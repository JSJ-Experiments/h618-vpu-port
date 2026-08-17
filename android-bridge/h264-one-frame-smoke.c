// SPDX-License-Identifier: MIT
/*
 * One-frame H.264 hardware smoke test for the private Android VENC closure.
 * It is built with the public CedarX headers, but links vendor functions at
 * runtime so no proprietary library is committed or needed at build time.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "vencoder.h"

typedef struct ScMemOpsS *(*get_ops_fn)(void);
/* Android12's libvencoder acquires VE/MemAdapter globally in VideoEncCreate;
 * its base-config ABI is the six-field CedarX layout, not the later Linux
 * wrapper that prepends bEncH264Nalu and appends adapter pointers. */
typedef struct {
    unsigned int nInputWidth, nInputHeight, nDstWidth, nDstHeight, nStride;
    VENC_PIXEL_FMT eInputFormat;
    /* Android VENC reads a larger private record; keep its tail zeroed. */
    unsigned int private_tail[8];
} VendorBaseConfig;
/* H618 Android12's H264 parameter record places max-IDR interval before
 * bitrate; the older public header has those two fields reversed. */
typedef struct {
    VencH264ProfileLevel sProfileLevel;
    int bEntropyCodingCABAC;
    VencQPRange sQPRange;
    int nFramerate;
    int nMaxKeyInterval;
    int nBitrate;
    VENC_CODING_MODE nCodingMode;
} VendorH264Param;
typedef VideoEncoder *(*create_fn)(VENC_CODEC_TYPE);
typedef void (*destroy_fn)(VideoEncoder *);
typedef int (*init_fn)(VideoEncoder *, VendorBaseConfig *);
typedef int (*set_fn)(VideoEncoder *, VENC_INDEXTYPE, void *);
typedef int (*alloc_fn)(VideoEncoder *, VencAllocateBufferParam *);
typedef int (*get_input_fn)(VideoEncoder *, VencInputBuffer *);
typedef int (*flush_fn)(VideoEncoder *, VencInputBuffer *);
typedef int (*add_fn)(VideoEncoder *, VencInputBuffer *);
typedef int (*encode_fn)(VideoEncoder *);
typedef int (*get_output_fn)(VideoEncoder *, VencOutputBuffer *);
typedef int (*free_output_fn)(VideoEncoder *, VencOutputBuffer *);
typedef int (*release_input_fn)(VideoEncoder *);
typedef int (*uninit_fn)(VideoEncoder *);

#define LOAD(handle, name, type) ((type)dlsym((handle), (name)))

int main(int argc, char **argv)
{
    void *memlib = dlopen("libMemAdapter.so", RTLD_NOW | RTLD_GLOBAL);
    void *venclib = dlopen("libvencoder.so", RTLD_NOW | RTLD_GLOBAL);
    get_ops_fn get_ops; create_fn create; destroy_fn destroy; init_fn init;
    set_fn set; alloc_fn alloc; get_input_fn get_input; flush_fn flush;
    add_fn add; encode_fn encode; get_output_fn get_output; free_output_fn free_output;
    release_input_fn release_input; uninit_fn uninit;
    struct ScMemOpsS *memops; VideoEncoder *encoder = NULL;
    VendorBaseConfig config; VendorH264Param h264; VencAllocateBufferParam buffers;
    VencInputBuffer input; VencOutputBuffer output;
    unsigned int vbv_size;
    int width = 320, height = 240, frames = 1, frame;
    int rc = 1;
    if (argc != 1 && argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s [width height [frames]]\n", argv[0]); return 64;
    }
    if (argc >= 3) { width = atoi(argv[1]); height = atoi(argv[2]); }
    if (argc == 4) frames = atoi(argv[3]);
    if (width <= 0 || height <= 0 || (width & 1) || (height & 1) || frames <= 0 || frames > 1000) {
        fprintf(stderr, "invalid dimensions or frame count\n"); return 64;
    }
    /* The legacy bridge is only validated at 320x240. Larger VENC init paths
     * can destabilize this experimental kernel driver, so require an explicit
     * opt-in while reverse-engineering continues. */
    if ((width > 320 || height > 240) && !getenv("H618_UNSAFE_EXPERIMENTAL")) {
        fprintf(stderr, "refusing unvalidated VENC size; set H618_UNSAFE_EXPERIMENTAL=1 to override\n");
        return 64;
    }

    if (!memlib || !venclib) { fprintf(stderr, "dlopen: %s\n", dlerror()); goto out; }
    get_ops = LOAD(memlib, "MemAdapterGetOpsS", get_ops_fn);
    create = LOAD(venclib, "VideoEncCreate", create_fn); destroy = LOAD(venclib, "VideoEncDestroy", destroy_fn);
    init = LOAD(venclib, "VideoEncInit", init_fn); set = LOAD(venclib, "VideoEncSetParameter", set_fn);
    alloc = LOAD(venclib, "AllocInputBuffer", alloc_fn); get_input = LOAD(venclib, "GetOneAllocInputBuffer", get_input_fn);
    flush = LOAD(venclib, "FlushCacheAllocInputBuffer", flush_fn); add = LOAD(venclib, "AddOneInputBuffer", add_fn);
    encode = LOAD(venclib, "VideoEncodeOneFrame", encode_fn); get_output = LOAD(venclib, "GetOneBitstreamFrame", get_output_fn);
    free_output = LOAD(venclib, "FreeOneBitStreamFrame", free_output_fn); release_input = LOAD(venclib, "ReleaseAllocInputBuffer", release_input_fn);
    uninit = LOAD(venclib, "VideoEncUnInit", uninit_fn);
    if (!get_ops || !create || !destroy || !init || !set || !alloc || !get_input || !flush || !add || !encode || !get_output || !free_output || !release_input || !uninit) {
        fprintf(stderr, "VENC ABI incomplete: %s\n", dlerror()); goto out;
    }
    memops = get_ops();
    if (!memops || memops->open()) { fprintf(stderr, "MemAdapter open failed\n"); goto out; }
    vbv_size = width * height * 2;
    if (vbv_size < 12 * 1024 * 1024) vbv_size = 12 * 1024 * 1024;
    memset(&config, 0, sizeof(config));
    config.nInputWidth = config.nDstWidth = config.nStride = width;
    config.nInputHeight = config.nDstHeight = height;
    config.eInputFormat = VENC_PIXEL_YUV420SP;
    encoder = create(VENC_CODEC_H264);
    if (!encoder) { fprintf(stderr, "VideoEncCreate failed\n"); goto close_mem; }
    memset(&h264, 0, sizeof(h264));
    h264.sProfileLevel.nProfile = VENC_H264ProfileBaseline;
    h264.sProfileLevel.nLevel = width * height > 1920 * 1080 ? VENC_H264Level51 : VENC_H264Level4;
    h264.sQPRange.nMinqp = 20; h264.sQPRange.nMaxqp = 45;
    h264.nFramerate = 25; h264.nBitrate = width * height; h264.nMaxKeyInterval = 25;
    if (set(encoder, VENC_IndexParamH264Param, &h264)) {
        fprintf(stderr, "VideoEncSetParameter(H264) failed\n"); goto destroy;
    }
    if (set(encoder, VENC_IndexParamSetVbvSize, &vbv_size)) {
        fprintf(stderr, "VideoEncSetParameter(VBV) failed\n"); goto destroy;
    }
    {
        int init_result = init(encoder, &config);
        if (init_result) { fprintf(stderr, "VideoEncInit failed: %d\n", init_result); goto destroy; }
    }
    memset(&buffers, 0, sizeof(buffers)); buffers.nBufferNum = 1; buffers.nSizeY = width * height; buffers.nSizeC = width * height / 2;
    if (alloc(encoder, &buffers)) { fprintf(stderr, "input allocation failed\n"); goto uninit; }
    { struct timespec start, end; unsigned long total = 0; clock_gettime(CLOCK_MONOTONIC, &start);
    for (frame = 0; frame < frames; frame++) {
        memset(&input, 0, sizeof(input));
        if (get_input(encoder, &input)) { fprintf(stderr, "input dequeue failed\n"); goto release_input; }
        memset(input.pAddrVirY, 16 + (frame & 15), buffers.nSizeY); memset(input.pAddrVirC, 128, buffers.nSizeC);
        if (flush(encoder, &input) || add(encoder, &input) || encode(encoder)) { fprintf(stderr, "frame encode failed at %d\n", frame); goto release_input; }
        memset(&output, 0, sizeof(output));
        if (get_output(encoder, &output) || (!output.nSize0 && !output.nSize1)) { fprintf(stderr, "no H264 bitstream returned at %d\n", frame); goto release_input; }
        total += output.nSize0 + output.nSize1; free_output(encoder, &output);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    { double seconds = (end.tv_sec-start.tv_sec) + (end.tv_nsec-start.tv_nsec)/1e9;
      printf("H264 hardware encode OK: %dx%d, %d frame(s), %lu bytes, %.2f fps\n", width,height,frames,total,frames/(seconds > 0 ? seconds : 1e-9)); }
    rc = 0; }
release_input: release_input(encoder);
uninit: uninit(encoder);
destroy: destroy(encoder);
close_mem: memops->close();
out: if (venclib) dlclose(venclib); if (memlib) dlclose(memlib); return rc;
}

// SPDX-License-Identifier: MIT
/*
 * One-frame H.264 hardware smoke test for the private Android VENC closure.
 * It is built with the public CedarX headers, but links vendor functions at
 * runtime so no proprietary library is committed or needed at build time.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "vencoder.h"

typedef struct ScMemOpsS *(*get_ops_fn)(void);
/* Android12's libvencoder acquires VE/MemAdapter globally in VideoEncCreate;
 * its base-config ABI is the six-field CedarX layout, not the later Linux
 * wrapper that prepends bEncH264Nalu and appends adapter pointers. */
typedef struct {
    unsigned int nInputWidth, nInputHeight, nDstWidth, nDstHeight, nStride;
    VENC_PIXEL_FMT eInputFormat;
} VendorBaseConfig;
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

int main(void)
{
    void *memlib = dlopen("libMemAdapter.so", RTLD_NOW | RTLD_GLOBAL);
    void *venclib = dlopen("libvencoder.so", RTLD_NOW | RTLD_GLOBAL);
    get_ops_fn get_ops; create_fn create; destroy_fn destroy; init_fn init;
    set_fn set; alloc_fn alloc; get_input_fn get_input; flush_fn flush;
    add_fn add; encode_fn encode; get_output_fn get_output; free_output_fn free_output;
    release_input_fn release_input; uninit_fn uninit;
    struct ScMemOpsS *memops; VideoEncoder *encoder = NULL;
    VendorBaseConfig config; VencH264Param h264; VencAllocateBufferParam buffers;
    VencInputBuffer input; VencOutputBuffer output;
    unsigned int vbv_size = 12 * 1024 * 1024;
    const int width = 320, height = 240;
    int rc = 1;

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
    memset(&config, 0, sizeof(config));
    config.nInputWidth = config.nDstWidth = config.nStride = width;
    config.nInputHeight = config.nDstHeight = height;
    config.eInputFormat = VENC_PIXEL_YUV420SP;
    encoder = create(VENC_CODEC_H264);
    if (!encoder) { fprintf(stderr, "VideoEncCreate failed\n"); goto close_mem; }
    memset(&h264, 0, sizeof(h264));
    h264.sProfileLevel.nProfile = VENC_H264ProfileBaseline;
    h264.sProfileLevel.nLevel = VENC_H264Level3;
    h264.sQPRange.nMinqp = 20; h264.sQPRange.nMaxqp = 45;
    h264.nFramerate = 30; h264.nBitrate = 500000; h264.nMaxKeyInterval = 30;
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
    memset(&input, 0, sizeof(input));
    if (get_input(encoder, &input)) { fprintf(stderr, "input dequeue failed\n"); goto release_input; }
    memset(input.pAddrVirY, 16, buffers.nSizeY); memset(input.pAddrVirC, 128, buffers.nSizeC);
    if (flush(encoder, &input) || add(encoder, &input) || encode(encoder)) { fprintf(stderr, "frame encode failed\n"); goto release_input; }
    memset(&output, 0, sizeof(output));
    if (get_output(encoder, &output) || (!output.nSize0 && !output.nSize1)) { fprintf(stderr, "no H264 bitstream returned\n"); goto release_input; }
    printf("H264 hardware encode OK: %u + %u bytes%s\n", output.nSize0, output.nSize1, (output.nFlag & VENC_BUFFERFLAG_KEYFRAME) ? " keyframe" : "");
    free_output(encoder, &output); rc = 0;
release_input: release_input(encoder);
uninit: uninit(encoder);
destroy: destroy(encoder);
close_mem: memops->close();
out: if (venclib) dlclose(venclib); if (memlib) dlclose(memlib); return rc;
}

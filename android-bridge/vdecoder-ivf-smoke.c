// SPDX-License-Identifier: MIT
/* Feed an IVF VP9 sequence frame-by-frame to the Android CedarX oracle. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void VideoDecoder;
typedef struct {
 int eCodecFormat,nWidth,nHeight,nFrameRate,nFrameDuration,nAspectRatio,bIs3DStream,nCodecSpecificDataLen;
 char *pCodecSpecificData;
 int bSecureStreamFlag,bSecureStreamFlagLevel1,bIsFramePackage,h265ReferencePictureNum,bReOpenEngine,bIsFrameCtsTestFlag;
} VideoStreamInfo;
typedef struct {
 int bScaleDownEn,bRotationEn,bSecOutputEn,nHorizonScaleDownRatio,nVerticalScaleDownRatio,nSDWidth,nSDHeight,bAnySizeSD,nSecHorizonScaleDownRatio,nSecVerticalScaleDownRatio,nRotateDegree,bThumbnailMode,eOutputPixelFormat,eSecOutputPixelFormat,bNoBFrames,bDisable3D,bSupportMaf,bDispErrorFrame,nVbvBufferSize,nFrameBufferNum,bSecureosEn,bGpuBufValid,nAlignStride,bIsSoftDecoderFlag,bVirMallocSbm,bSupportPallocBufBeforeDecode,nDeInterlaceHoldingFrameBufferNum,nDisplayHoldingFrameBufferNum,nRotateHoldingFrameBufferNum,nDecodeSmoothFrameBufferNum,bIsTvStream,bAdapteDropFrame;
 void *memops; int eCtlAfbcMode,eCtlIptvMode; void *veOpsS,*pVeOpsSelf; int bConvertVp910bitTo8bit; unsigned int nVeFreq; int bCalledByOmxFlag,bSetProcInfoEnable,nSetProcInfoFreq,nChannelNum,nSupportMaxWidth,nSupportMaxHeight,commonConfigFlag,bATMFlag;
} VConfig;
typedef void (*add_plugins_fn)(void);
typedef VideoDecoder *(*create_fn)(void);
typedef void (*destroy_fn)(VideoDecoder *);
typedef int (*init_fn)(VideoDecoder *, VideoStreamInfo *, VConfig *);
typedef int (*request_stream_fn)(VideoDecoder *, int, char **, int *, char **, int *, int);
typedef int (*submit_stream_fn)(VideoDecoder *, void *, int);
typedef int (*decode_fn)(VideoDecoder *, int, int, int, long long);
typedef void *(*request_picture_fn)(VideoDecoder *, int);
typedef int (*return_picture_fn)(VideoDecoder *, void *);
typedef void (*dump_all_tagged_fn)(const char *);

static uint16_t le16(const unsigned char *p)
{
 return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t le32(const unsigned char *p)
{
 return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const unsigned char *p)
{
 return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static void dump_buffers(const char *tag)
{
 void *memadapter = dlopen("libMemAdapter.so", RTLD_NOW | RTLD_NOLOAD);
 dump_all_tagged_fn dump = NULL;

 if (!memadapter)
  return;
 *(void **)(&dump) = dlsym(memadapter, "MemAdapterDumpAllTagged");
 if (dump)
  dump(tag);
 dlclose(memadapter);
}

int main(int argc, char **argv)
{
 unsigned char header[32], packet[12];
 VideoStreamInfo stream;
 VConfig config;
 FILE *input;
 void *library;
 VideoDecoder *decoder;
 add_plugins_fn add_plugins;
 create_fn create;
 destroy_fn destroy;
 init_fn init;
 request_stream_fn request_stream;
 submit_stream_fn submit_stream;
 decode_fn decode;
 request_picture_fn request_picture;
 return_picture_fn return_picture;
 unsigned int frame_count, frame_index;
 int result = 1;

 if (argc != 2) {
  fprintf(stderr, "usage: %s sequence.ivf\n", argv[0]);
  return 64;
 }
 input = fopen(argv[1], "rb");
 if (!input || fread(header, 1, sizeof(header), input) != sizeof(header) ||
     memcmp(header, "DKIF", 4) || memcmp(header + 8, "VP90", 4)) {
  fprintf(stderr, "invalid VP9 IVF input\n");
  return 65;
 }
 frame_count = le32(header + 24);

 library = dlopen("libvdecoder.so", RTLD_NOW | RTLD_LOCAL);
 if (!library) {
  fprintf(stderr, "dlopen: %s\n", dlerror());
  return 1;
 }
#define LOAD(symbol, name) do { *(void **)(&name) = dlsym(library, symbol); } while (0)
 LOAD("AddVDPlugin", add_plugins);
 LOAD("CreateVideoDecoder", create);
 LOAD("DestroyVideoDecoder", destroy);
 LOAD("InitializeVideoDecoder", init);
 LOAD("RequestVideoStreamBuffer", request_stream);
 LOAD("SubmitVideoStreamData", submit_stream);
 LOAD("DecodeVideoStream", decode);
 LOAD("RequestPicture", request_picture);
 LOAD("ReturnPicture", return_picture);
#undef LOAD
 if (!add_plugins || !create || !destroy || !init || !request_stream ||
     !submit_stream || !decode || !request_picture || !return_picture) {
  fprintf(stderr, "missing decoder API\n");
  return 2;
 }

 if (getenv("CEDAR_VENDOR_LOG_LEVEL")) {
  int *level = (int *)dlsym(RTLD_DEFAULT, "GLOBAL_LOG_LEVEL");

  if (level)
   *level = atoi(getenv("CEDAR_VENDOR_LOG_LEVEL"));
 }

 add_plugins();
 memset(&stream, 0, sizeof(stream));
 memset(&config, 0, sizeof(config));
 stream.eCodecFormat = 0x113;
 stream.nWidth = le16(header + 12);
 stream.nHeight = le16(header + 14);
 stream.nFrameRate = 30;
 stream.nFrameDuration = 33333;
 stream.bIsFramePackage = 1;
 config.eOutputPixelFormat = 6;
 config.nVbvBufferSize = 2 * 1024 * 1024;
 config.nFrameBufferNum = 8;
 decoder = create();
 if (!decoder || init(decoder, &stream, &config)) {
  fprintf(stderr, "decoder initialization failed\n");
  return 3;
 }

 for (frame_index = 0; frame_index < frame_count; frame_index++) {
  uint32_t size;
  uint64_t timestamp;
  unsigned char *payload;
  char *first = NULL, *second = NULL;
  int first_size = 0, second_size = 0, iteration, rc;
  struct {
   char *pData; int nLength; long long nPts; long long nPcr;
   int bIsFirstPart,bIsLastPart,nID,nStreamIndex,bValid;
   unsigned int bVideoInfoFlag; void *pVideoInfo;
  } data;

  if (fread(packet, 1, sizeof(packet), input) != sizeof(packet)) {
   fprintf(stderr, "truncated IVF frame header %u\n", frame_index);
   goto out;
  }
  size = le32(packet);
  timestamp = le64(packet + 4);
  payload = malloc(size);
  if (!size || !payload || fread(payload, 1, size, input) != size) {
   fprintf(stderr, "truncated IVF payload %u\n", frame_index);
   free(payload);
   goto out;
  }
  rc = request_stream(decoder, (int)size, &first, &first_size,
                      &second, &second_size, 0);
  if (rc || first_size + second_size < (int)size) {
   fprintf(stderr, "RequestVideoStreamBuffer frame=%u rc=%d sizes=%d+%d\n",
           frame_index, rc, first_size, second_size);
   free(payload);
   goto out;
  }
  if (first_size >= (int)size)
   memcpy(first, payload, size);
  else {
   memcpy(first, payload, first_size);
   memcpy(second, payload + first_size, size - first_size);
  }
  free(payload);
  memset(&data, 0, sizeof(data));
  data.pData = first;
  data.nLength = (int)size;
  data.nPts = (long long)timestamp * 33333;
  data.nPcr = -1;
  data.bIsFirstPart = 1;
  data.bIsLastPart = 1;
  data.nID = (int)frame_index;
  data.bValid = 1;
  if (submit_stream(decoder, &data, 0)) {
   fprintf(stderr, "SubmitVideoStreamData frame=%u failed\n", frame_index);
   goto out;
  }

  for (iteration = 0; iteration < 200; iteration++) {
   void *picture;
   char tag[32];

   rc = decode(decoder,
               frame_index + 1 == frame_count && iteration > 20,
               0, 0, data.nPts);
   picture = request_picture(decoder, 0);
   if (picture) {
    const char *dump_frame = getenv("CEDAR_BRIDGE_DUMP_FRAME");

    snprintf(tag, sizeof(tag), "frame-%03u", frame_index);
    if (!dump_frame || !*dump_frame ||
        frame_index == strtoul(dump_frame, NULL, 0))
     dump_buffers(tag);
    return_picture(decoder, picture);
    printf("CedarX VP9 frame %u/%u OK (size=%u result=%d)\n",
           frame_index + 1, frame_count, size, rc);
    break;
   }
   usleep(10000);
  }
  if (iteration == 200) {
   fprintf(stderr, "no decoded picture for frame %u\n", frame_index);
   goto out;
  }
 }
 result = 0;
out:
 destroy(decoder);
 fclose(input);
 return result;
}

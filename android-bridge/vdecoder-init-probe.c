// SPDX-License-Identifier: MIT
/* Exercise private Android CedarX H.264 decoder initialization on Linux CMA. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void VideoDecoder;
typedef struct {
 int eCodecFormat,nWidth,nHeight,nFrameRate,nFrameDuration,nAspectRatio,bIs3DStream,nCodecSpecificDataLen;
 char *pCodecSpecificData;
 int bSecureStreamFlag,bSecureStreamFlagLevel1,bIsFramePackage,h265ReferencePictureNum,bReOpenEngine,bIsFrameCtsTestFlag;
} VideoStreamInfo;
typedef struct {
 int bScaleDownEn,bRotationEn,bSecOutputEn,nHorizonScaleDownRatio,nVerticalScaleDownRatio,nSecHorizonScaleDownRatio,nSecVerticalScaleDownRatio,nRotateDegree,bThumbnailMode,eOutputPixelFormat,eSecOutputPixelFormat,bNoBFrames,bDisable3D,bSupportMaf,bDispErrorFrame,nVbvBufferSize,nFrameBufferNum,bSecureosEn,bGpuBufValid,nAlignStride,bIsSoftDecoderFlag,bVirMallocSbm,bSupportPallocBufBeforeDecode,nDeInterlaceHoldingFrameBufferNum,nDisplayHoldingFrameBufferNum,nRotateHoldingFrameBufferNum,nDecodeSmoothFrameBufferNum,bIsTvStream;
 void *memops; int eCtlAfbcMode,eCtlIptvMode; void *veOpsS,*pVeOpsSelf; int bConvertVp910bitTo8bit; unsigned int nVeFreq; int bCalledByOmxFlag,bSetProcInfoEnable,nSetProcInfoFreq,nChannelNum;
} VConfig;
typedef VideoDecoder *(*create_fn)(void);
typedef void (*destroy_fn)(VideoDecoder *);
typedef int (*init_fn)(VideoDecoder *, VideoStreamInfo *, VConfig *);
int main(void) {
 void *lib=dlopen("libvdecoder.so",RTLD_NOW|RTLD_LOCAL); VideoDecoder *d; VideoStreamInfo s; VConfig c;
 create_fn create; destroy_fn destroy; init_fn init;
 if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
 *(void **)(&create)=dlsym(lib,"CreateVideoDecoder"); *(void **)(&destroy)=dlsym(lib,"DestroyVideoDecoder"); *(void **)(&init)=dlsym(lib,"InitializeVideoDecoder");
 if(!create||!destroy||!init){fprintf(stderr,"missing API\n");return 2;}
 memset(&s,0,sizeof(s)); memset(&c,0,sizeof(c)); s.eCodecFormat=0x115; s.nWidth=320; s.nHeight=240; s.nFrameRate=30; s.nFrameDuration=33333; s.bIsFramePackage=1; c.eOutputPixelFormat=6; c.nVbvBufferSize=2*1024*1024; c.nFrameBufferNum=4;
 d=create(); if(!d){fprintf(stderr,"create failed\n");return 3;} int rc=init(d,&s,&c); destroy(d); dlclose(lib); if(rc){fprintf(stderr,"InitializeVideoDecoder rc=%d\n",rc);return 4;} puts("Android CedarX H264 decoder initialization OK"); return 0;
}

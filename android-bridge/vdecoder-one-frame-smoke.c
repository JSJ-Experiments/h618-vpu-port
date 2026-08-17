// SPDX-License-Identifier: MIT
/* Decode an Annex-B H.264 access unit using private Android CedarX on Linux CMA. */
#include <dlfcn.h>
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
int main(int argc, char **argv) {
 int codec = 0x115, width = 320, height = 240;
 void *lib; VideoDecoder *d; VideoStreamInfo s; VConfig c; FILE *f; long n; char *src, *a, *b; int as, bs, rc, i;
 add_plugins_fn add_plugins; create_fn create; destroy_fn destroy; init_fn init; request_stream_fn request_stream; submit_stream_fn submit_stream; decode_fn decode; request_picture_fn request_picture; return_picture_fn return_picture;
 if (argc != 2 && argc != 5) { fprintf(stderr, "usage: %s bitstream [codec-id width height]\n", argv[0]); return 64; }
 if (argc == 5) { codec=(int)strtol(argv[2],NULL,0); width=atoi(argv[3]); height=atoi(argv[4]); }
 f=fopen(argv[1], "rb"); if(!f){perror(argv[1]);return 65;} fseek(f,0,SEEK_END); n=ftell(f); rewind(f); if(n<=0 || n>16*1024*1024){fprintf(stderr,"invalid input size\n");return 66;} src=malloc((size_t)n); if(!src || fread(src,1,(size_t)n,f)!=(size_t)n){fprintf(stderr,"read failed\n");return 67;} fclose(f);
 lib=dlopen("libvdecoder.so",RTLD_NOW|RTLD_LOCAL); if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
 *(void **)(&add_plugins)=dlsym(lib,"AddVDPlugin"); *(void **)(&create)=dlsym(lib,"CreateVideoDecoder"); *(void **)(&destroy)=dlsym(lib,"DestroyVideoDecoder"); *(void **)(&init)=dlsym(lib,"InitializeVideoDecoder"); *(void **)(&request_stream)=dlsym(lib,"RequestVideoStreamBuffer"); *(void **)(&submit_stream)=dlsym(lib,"SubmitVideoStreamData"); *(void **)(&decode)=dlsym(lib,"DecodeVideoStream"); *(void **)(&request_picture)=dlsym(lib,"RequestPicture"); *(void **)(&return_picture)=dlsym(lib,"ReturnPicture");
 if(!add_plugins||!create||!destroy||!init||!request_stream||!submit_stream||!decode||!request_picture||!return_picture){fprintf(stderr,"missing decoder API\n");return 2;}
 add_plugins(); memset(&s,0,sizeof(s)); memset(&c,0,sizeof(c)); s.eCodecFormat=codec; s.nWidth=width; s.nHeight=height; s.nFrameRate=30; s.nFrameDuration=33333; s.bIsFramePackage=0; c.eOutputPixelFormat=6; c.nVbvBufferSize=2*1024*1024; c.nFrameBufferNum=4;
 d=create(); if(!d){fprintf(stderr,"create failed\n");return 3;} rc=init(d,&s,&c); if(rc){fprintf(stderr,"InitializeVideoDecoder rc=%d\n",rc);destroy(d);return 4;}
 a=b=NULL; as=bs=0; rc=request_stream(d,(int)n,&a,&as,&b,&bs,0); if(rc || as+bs<n){fprintf(stderr,"RequestVideoStreamBuffer rc=%d sizes=%d+%d\n",rc,as,bs);destroy(d);return 5;} if(as>=n) memcpy(a,src,(size_t)n); else {memcpy(a,src,(size_t)as);memcpy(b,src+as,(size_t)(n-as));}
 { struct { char *pData; int nLength; long long nPts; long long nPcr; int bIsFirstPart,bIsLastPart,nID,nStreamIndex,bValid; unsigned int bVideoInfoFlag; void *pVideoInfo; } data; memset(&data,0,sizeof(data)); data.pData=a; data.nLength=(int)n; data.nPts=0; data.nPcr=-1; data.bIsFirstPart=1; data.bIsLastPart=1; data.bValid=1; if(submit_stream(d,&data,0)){fprintf(stderr,"SubmitVideoStreamData failed\n");destroy(d);return 6;} }
 for(i=0;i<300;i++){ void *pic; rc=decode(d,i>20,0,0,0); pic=request_picture(d,0); if(pic){return_picture(d,pic); printf("Android CedarX hardware decode OK (codec=0x%x result=%d)\n", codec, rc);destroy(d);free(src);return 0;} usleep(10000); }
 fprintf(stderr,"no decoded picture\n");destroy(d);free(src);return 7;
}

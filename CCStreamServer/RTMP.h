#include "rtmp.h"
#include "rtmp_sys.h"
#include "amf.h"

typedef char bool;

#define TRUE 1
#define FALSE 0

// H.264 NAL type
typedef enum
{
    H264NT_NAL = 0,
    H264NT_SLICE,
    H264NT_SLICE_DPA,
    H264NT_SLICE_DPB,
    H264NT_SLICE_DPC,
    H264NT_SLICE_IDR,
    H264NT_SEI,
    H264NT_SPS,
    H264NT_PPS,
}H264NALTYPE;

// NALU单元
typedef struct _NaluUnit
{
    H264NALTYPE type;
    int size;
    unsigned char *data;
}NaluUnit;

typedef struct _RTMPMetadata
{
    // video, must be h264 type
    unsigned int    nWidth;
    unsigned int    nHeight;
    unsigned int    nFrameRate;   // fps
    unsigned int    nVideoDataRate; // bps
    unsigned int    nSpsLen;
    unsigned char   Sps[1024];
    unsigned int    nPpsLen;
    unsigned char   Pps[1024];
}RTMPMetadata, *LPRTMPMetadata;

// 视频帧信息结构体（根据main.c中h264_stream_get等函数推测补充）
typedef struct _video_frame_info
{
    unsigned char *frame_data;
    unsigned int frame_len;
}video_frame_info;

// H.264参数结构体（根据main.c中h264_stream_param_get函数推测补充）
typedef struct _h264_param
{
    unsigned int width;
    unsigned int height;
    unsigned int fps;
}h264_param;

// 函数声明（根据main.c中的函数调用补充）
void h264_stream_init(void);
void h264_stream_deinit(void);
void h264_stream_get(video_frame_info *info);
int h264_stream_param_get(h264_param *param);
void aac_stream_init(void);
void aac_stream_deinit(void);
int aac_stream_get(char *buf, int size);
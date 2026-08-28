#ifndef CCRTMPDEF_H
#define CCRTMPDEF_H

// ============================================================================
// RTMP / H.264 / AAC Type Definitions
// Compatible with librtmp
// ============================================================================

#include "rtmp.h"
#include "rtmp_sys.h"
#include "amf.h"

// ============================================================================
// H.264 NAL Types
// ============================================================================

typedef enum
{
    H264NT_NAL       = 0,
    H264NT_SLICE     = 1,
    H264NT_SLICE_DPA = 2,
    H264NT_SLICE_DPB = 3,
    H264NT_SLICE_DPC = 4,
    H264NT_SLICE_IDR = 5,
    H264NT_SEI       = 6,
    H264NT_SPS       = 7,
    H264NT_PPS       = 8,
} H264NALTYPE;

// ============================================================================
// NALU Unit
// ============================================================================

typedef struct _NaluUnit
{
    H264NALTYPE type;
    int         size;
    uint8_t*    data;
} NaluUnit;

// ============================================================================
// RTMP Metadata
// ============================================================================

typedef struct _RTMPMetadata
{
    uint32_t    nWidth;
    uint32_t    nHeight;
    uint32_t    nFrameRate;       // fps
    uint32_t    nVideoDataRate;   // bps
    uint32_t    nSpsLen;
    uint8_t     Sps[1024];
    uint32_t    nPpsLen;
    uint8_t     Pps[1024];
} RTMPMetadata;

// ============================================================================
// Video Frame Info
// ============================================================================

typedef struct _VideoFrameInfo
{
    uint8_t* frame_data;
    uint32_t frame_len;
} VideoFrameInfo;

// ============================================================================
// H.264 Stream API
// ============================================================================

void h264_stream_init(void);
void h264_stream_deinit(void);
void h264_stream_get(VideoFrameInfo* info);

typedef struct _H264Param
{
    uint32_t width;
    uint32_t height;
    uint32_t fps;
} H264Param;

int h264_stream_param_get(H264Param* param);

// ============================================================================
// AAC Stream API
// ============================================================================

void aac_stream_init(void);
void aac_stream_deinit(void);
int  aac_stream_get(char* buf, int size);

#endif // CCRTMPDEF_H

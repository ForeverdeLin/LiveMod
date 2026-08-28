#ifndef _VIDEO_CAPTURE_H_
#define _VIDEO_CAPTURE_H_

#include <stddef.h>
#include <linux/videodev2.h>
#include "H264Encoder.h"

struct buffer {
    void *start;
    size_t length;
};

/**
 * @brief V4L2摄像头设备结构体
 * @details 用于存储摄像头设备配置参数、采集缓冲区信息及编码数据，
 *          整合了V4L2设备控制、原始图像采集和H.264编码功能
 */
typedef struct v4lCamera {
    const char        *device_name;      // 摄像头设备路径（如/dev/video0）
    int                fd;               // 摄像头设备文件描述符
    int                width;            // 视频宽度（像素）
    int                height;           // 视频高度（像素）
    int                display_depth;    // 像素格式深度（如5对应RGB24格式）
    unsigned char      *h264_buf;        // H.264编码后数据缓冲区
    unsigned int       h264_length;      // H.264编码数据长度（字节）
    int                buff_num;         // 采集缓冲区数量
    struct buffer      *buffers;         // 采集缓冲区数组指针（存储原始图像数据）
    struct v4l2_capability v4l2_cap;     // V4L2设备能力结构体（设备支持的功能）
    struct v4l2_cropcap v4l2_cropcap;   // V4L2裁剪能力结构体（支持的裁剪区域）
    struct v4l2_format  v4l2_fmt;        // V4L2视频格式结构体（像素格式、分辨率等）
    struct v4l2_crop    crop;            // V4L2裁剪参数结构体（当前裁剪区域配置）
    X264Encoder        encoder;          // H.264编码器实例（基于X264库）
} IOTC_Camera;

void camera_init(IOTC_Camera *cam);
int camera_open(IOTC_Camera *cam);
void camera_capturing_start(IOTC_Camera *cam);
void camera_capturing_stop(IOTC_Camera *cam);
int read_and_encode_frame(IOTC_Camera *cam);
int capture_frame_yuyv(IOTC_Camera *cam, uint8_t* dst, size_t dstSize, size_t* outSize);
void camera_uninit(IOTC_Camera *cam);
int camera_close(IOTC_Camera *cam);

#endif
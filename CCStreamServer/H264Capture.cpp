/**
 * @file H264Capture.cpp
 * @brief V4L2摄像头采集实现
 * @details 使用V4L2（Video4Linux2）API从Linux摄像头设备采集视频
 *          知识点：V4L2 API、内存映射（mmap）、视频设备操作
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <dirent.h>
#include <stdio.h>
#include <fcntl.h>              /* low-level i/o */
#include <asm/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>

#include "H264Capture.h"

/**
 * @brief 摄像头IO控制函数（带EINTR重试）
 * @details 知识点：系统调用中断处理
 *          EINTR：系统调用被信号中断
 *          需要重试，直到成功或非EINTR错误
 * @param fd 设备文件描述符
 * @param request IO控制命令（如VIDIOC_REQBUFS）
 * @param arg 命令参数
 * @return ioctl返回值
 */
static inline int camera_ioctl(int fd, int request, void *arg)
{
    int r = -1;

    // 知识点：EINTR重试循环
    // 如果ioctl被信号中断（EINTR），需要重试
    do {
        r = ioctl(fd, request, arg);
    } while (r < 0 && EINTR == errno);

    return r;
}

/**
 * @brief 打开摄像头设备
 * @details 知识点：V4L2设备打开流程
 *          1. 检查设备文件是否存在（stat）
 *          2. 验证是否为字符设备（S_ISCHR）
 *          3. 打开设备文件（open）
 * @param cam 摄像头结构体指针
 * @return 0=成功，-1=失败
 */
int camera_open(IOTC_Camera* cam)
{
    struct stat st;//这是一个文件状态结构体变量，用于存储文件/设备的详细信息。

    // 知识点：检查设备文件
    // stat - 获取文件状态信息
    if (stat(cam->device_name, &st) < 0) {
        printf("Cannot identify '%s': %d, %s\n", cam->device_name,
               errno, strerror(errno));
        return -1;
    }

    // 知识点：验证设备类型
    // S_ISCHR - 检查是否为字符设备（设备文件）
    // V4L2设备必须是字符设备
    if (!S_ISCHR(st.st_mode)) {
        printf("%s is no device\n", cam->device_name);
        return -1;
    }

    // 知识点：打开设备文件
    // open - 打开设备文件
    // O_RDWR: 读写模式
    // 返回：文件描述符，<0=失败
    cam->fd = open(cam->device_name, O_RDWR, 0);//文件权限创建新文件才设置，这里是打开已有文件
    if (cam->fd < 0) {
        printf("Cannot open '%s': %d, %s\n", cam->device_name, errno, strerror(errno));
        return -1;
    }

    return 0;
}

int camera_close(IOTC_Camera* cam)
{
    if (cam->fd < 0)
        return -1;

    if (close(cam->fd) < 0) {
        printf("can not close camera!\n");
        return -1;
    }

    cam->fd = -1;

    return -1;
}

/**
 * @brief 初始化内存映射缓冲区
 * @details 知识点：V4L2内存映射（mmap）模式
 *          1. 请求缓冲区（VIDIOC_REQBUFS）
 *          2. 查询缓冲区信息（VIDIOC_QUERYBUF）
 *          3. 内存映射（mmap）
 *          4. 将缓冲区加入队列（VIDIOC_QBUF）
 *          
 *          内存映射模式优点：
 *          - 零拷贝：数据直接从内核空间映射到用户空间
 *          - 高性能：避免数据复制
 * @param cam 摄像头结构体指针
 */
static void camera_init_mmap(IOTC_Camera* cam)
{
    struct v4l2_requestbuffers req;  // 缓冲区请求结构
    struct v4l2_buffer buf;          // 缓冲区信息结构

    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 缓冲区类型：视频采集
    buf.memory = V4L2_MEMORY_MMAP;            // 内存模式：内存映射

    memset(&req, 0, sizeof(struct v4l2_requestbuffers));
    req.count = 4;                            // 请求4个缓冲区（双缓冲或三缓冲）
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;    // 缓冲区类型
    req.memory = V4L2_MEMORY_MMAP;            // 内存模式：内存映射

    // 知识点：请求缓冲区
    // VIDIOC_REQBUFS - 向驱动请求分配指定数量的缓冲区
    // 驱动会分配内核缓冲区，并返回实际分配的缓冲区数量
    if (camera_ioctl(cam->fd, VIDIOC_REQBUFS, &req) < 0) {
        if (EINVAL == errno) {
            printf("%s does not support memory mapping\n", cam->device_name);
            return;
        } else {
            printf("VIDIOC_REQBUFS\n");
            return;
        }
    }

    // 检查实际分配的缓冲区数量（至少需要2个，用于双缓冲）
    if (req.count < 2) {
        printf("Insufficient buffer memory on %s\n", cam->device_name);
        return;
    }

    // 分配缓冲区结构体数组（用于存储每个缓冲区的信息）
    cam->buffers = (struct buffer*)calloc(req.count, sizeof(*(cam->buffers)));
    if (!cam->buffers) {
        printf("Out of memory\n");
        return;
    }

    for (cam->buff_num = 0; cam->buff_num < req.count; cam->buff_num++) {
        buf.index = cam->buff_num;
        //查询缓冲区信息但这时候只知道分配了几个缓冲区，
        //不知道每个缓冲区的具体信息（大小、内存偏移地址等）
        if (camera_ioctl(cam->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            printf("VIDIOC_QUERYBUF\n");
            return;
        }
        //内存映射
        cam->buffers[cam->buff_num].length = buf.length;
        cam->buffers[cam->buff_num].start = mmap(NULL /* start anywhere */,
                                                buf.length,
                                                PROT_READ | PROT_WRITE
                                                /* required */,
                                                MAP_SHARED /* recommended */,
                                                cam->fd, buf.m.offset);
        //将缓冲区加入队列
        if (MAP_FAILED == cam->buffers[cam->buff_num].start) {
            printf("mmap\n");
            return;
        }
    }

    return;
}

/**
 * @brief 初始化摄像头设备
 * @details V4L2设备初始化核心函数，完成设备能力检查、参数配置及内存映射初始化
 *          主要流程：
 *          1. 查询设备能力（VIDIOC_QUERYCAP）
 *          2. 验证设备是否支持视频捕获和流式IO
 *          3. 配置视频裁剪参数（可选）
 *          4. 设置视频格式（分辨率、像素格式等）
 *          5. 初始化内存映射缓冲区
 * @param cam 摄像头结构体指针，包含设备信息及配置参数
 * @return 无返回值，通过错误码和打印信息指示初始化结果
 */
void camera_init(IOTC_Camera* cam)
{
    unsigned int min;
    // V4L2设备能力结构体指针（用于查询设备支持的功能）
    struct v4l2_capability *cap = &(cam->v4l2_cap);
    // 裁剪能力结构体指针（用于查询设备支持的裁剪范围）
    struct v4l2_cropcap *cropcap = &(cam->v4l2_cropcap);
    // 裁剪参数结构体指针（用于设置实际裁剪区域）
    struct v4l2_crop *crop = &(cam->crop);
    // 视频格式结构体指针（用于设置视频采集格式）
    struct v4l2_format *fmt = &(cam->v4l2_fmt);

    // 知识点：查询设备能力
    // VIDIOC_QUERYCAP - 获取设备基本信息和支持的功能
    // 成功返回0，失败返回-1并设置errno
    if (camera_ioctl(cam->fd, VIDIOC_QUERYCAP, cap) < 0) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", cam->device_name);
            return;
        } else {
            printf("VIDIOC_QUERYCAP\n");
            return;
        }
    }

    // 知识点：验证设备功能
    // 检查设备是否支持视频捕获功能（V4L2_CAP_VIDEO_CAPTURE）
    if (!(cap->capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n", cam->device_name);
        return;
    }

    // 检查设备是否支持流式IO（V4L2_CAP_STREAMING）
    // 流式IO是高效视频采集的必要条件（相比read/write方式）
    if (!(cap->capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n",
                cam->device_name);
        return;
    }

    // 打印设备信息（调试用）
    printf("\nVIDIOC_QUERYCAP\n");
    printf("the camera driver is %s\n", cap->driver);    // 驱动名称
    printf("the camera card is %s\n", cap->card);        // 设备名称
    printf("the camera bus info is %s\n", cap->bus_info); // 总线信息
    printf("the version is %d\n", cap->version);         // 驱动版本

    /* Select video input, video standard and tune here. */
    // 设置裁剪能力查询类型为视频捕获
    cropcap->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // 配置裁剪区域参数
    crop->c.width = cam->width;    // 裁剪宽度（与目标分辨率一致）
    crop->c.height = cam->height;  // 裁剪高度（与目标分辨率一致）
    crop->c.left = 0;              // 裁剪区域左上角X坐标
    crop->c.top = 0;               // 裁剪区域左上角Y坐标
    crop->type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 裁剪类型：视频捕获

    // 配置视频格式参数
    fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;          // 格式类型：视频捕获
    fmt->fmt.pix.width = cam->width;                  // 视频宽度（像素）
    fmt->fmt.pix.height = cam->height;                // 视频高度（像素）
    fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;     // 像素格式：YUYV（YUV422）
    fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;       // 扫描方式：隔行扫描

    // 知识点：设置视频格式
    // VIDIOC_S_FMT - 向驱动设置视频采集格式
    // 驱动可能会调整参数（如分辨率对齐），需后续检查实际设置值
    if (camera_ioctl(cam->fd, VIDIOC_S_FMT, fmt) < 0) {
        printf("VIDIOC_S_FMT\n");
        return;
    }

    /* Buggy driver paranoia. */
    // 处理可能存在问题的驱动：确保每行字节数至少为宽度*2（YUYV格式每个像素2字节）
    min = fmt->fmt.pix.width * 2;
    if (fmt->fmt.pix.bytesperline < min)
        fmt->fmt.pix.bytesperline = min;
    // 确保缓冲区大小至少为每行字节数*高度（一帧数据大小）
    min = fmt->fmt.pix.bytesperline * fmt->fmt.pix.height;
    if (fmt->fmt.pix.sizeimage < min)
        fmt->fmt.pix.sizeimage = min;

    // 初始化内存映射缓冲区（关键步骤：为采集数据分配内核缓冲区并映射到用户空间）
    camera_init_mmap(cam);

    return;
}

void camera_uninit(IOTC_Camera* cam)
{
    uint32_t i;

    for (i = 0; i < cam->buff_num; ++i) {
        if (munmap(cam->buffers[i].start, cam->buffers[i].length) < 0) {
            printf("munmap\n");
            return;
        }
    }

    free(cam->buffers);

    return;
}

/**
 * @brief 启动视频采集流程
 * @details V4L2视频采集启动核心函数，完成缓冲区入队和视频流开启
 *          主要流程：
 *          1. 将所有内存映射缓冲区加入采集队列（VIDIOC_QBUF）
 *          2. 发送开始流命令（VIDIOC_STREAMON）启动视频数据采集
 *          知识点：V4L2流式采集机制
 *                  - 缓冲区队列：驱动通过队列管理采集缓冲区，应用从队列取数据
 *                  - 流控制：VIDIOC_STREAMON/OFF控制采集的开始与停止
 * @param cam 摄像头结构体指针，包含已初始化的设备描述符和缓冲区信息
 */
void camera_capturing_start(IOTC_Camera* cam)
{
    uint32_t i;                                  // 缓冲区索引计数器
    struct v4l2_buffer buf;                      // V4L2缓冲区操作结构体
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 缓冲区类型：视频采集
    //该值是 V4L2（Video for Linux 2，Linux 下视频设备编程接口）中用于标识 “视频捕获” 
    //功能的枚举常量，即表示此缓冲区用于从视频设备（如摄像头）捕获视频数据。

    // 初始化缓冲区操作结构体
    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;      // 指定操作类型为视频采集
    buf.memory = V4L2_MEMORY_MMAP;               // 指定内存模式为内存映射

    // 知识点：缓冲区入队循环
    // 将所有已分配的内核缓冲区加入采集队列
    // 驱动会按队列顺序填充缓冲区数据
    for (i = 0; i < cam->buff_num; ++i) {
        buf.index = i;                           // 设置当前操作的缓冲区索引

        // 知识点：VIDIOC_QBUF - 将缓冲区交给驱动
        // 驱动接收后会在此缓冲区填充采集到的视频数据
        if (camera_ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) {
            printf("VIDIOC_QBUF failed: %d, %s\n", errno, strerror(errno));
            return;
        }
    }

    // 知识点：VIDIOC_STREAMON - 启动视频流采集
    // 驱动开始向队列中的缓冲区写入视频数据
    if (camera_ioctl(cam->fd, VIDIOC_STREAMON, &type) < 0) {
        printf("VIDIOC_STREAMON failed: %d, %s\n", errno, strerror(errno));
        return;
    }

    return;
}

void camera_capturing_stop(IOTC_Camera* cam)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (camera_ioctl(cam->fd, VIDIOC_STREAMOFF, &type) < 0) {
        printf("VIDIOC_STREAMOFF\n");
        return;
    }

    return;
}

static void camera_encode_frame(IOTC_Camera* cam, uint8_t * yuv_frame,
                               size_t yuv_length)
{
    int encLength = 0;

    encLength = h264_compress_frame(&cam->encoder, -1, yuv_frame, cam->h264_buf, false);
    cam->h264_length=encLength;

    return;
}

int capture_frame_yuyv(IOTC_Camera* cam, uint8_t* dst, size_t dstSize, size_t* outSize)
{
    if (!cam || !dst || dstSize == 0) {
        return -1;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (camera_ioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0) {
        switch (errno) {
        case EAGAIN:
            return 1;
        case EIO:
        default:
            return -1;
        }
    }

    size_t bytesToCopy = buf.bytesused;
    if (bytesToCopy > dstSize) {
        bytesToCopy = dstSize;
    }
    memcpy(dst, cam->buffers[buf.index].start, bytesToCopy);

    if (camera_ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) {
        printf("VIDIOC_QBUF ERROR\n");
        return -1;
    }

    if (outSize) {
        *outSize = bytesToCopy;
    }

    return 0;
}

int read_and_encode_frame(IOTC_Camera* cam)
{
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (camera_ioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0) {
        switch (errno) {
        case EAGAIN:
            return 0;
        case EIO:
            /* Could ignore EIO, see spec. */
            /* fall through */
        default:
            return -1;
        }
    }

    camera_encode_frame(cam, (uint8_t*)cam->buffers[buf.index].start, buf.length);

    if (camera_ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) {
        printf("VIDIOC_QBUF ERROR\n");
        return -1;
    }

    return 0;
}
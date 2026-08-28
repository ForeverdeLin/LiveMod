/**
 * @file FFmpegRTMPReceiver.cpp
 * @brief FFmpeg RTMP接收器实现
 * @details 使用FFmpeg库从RTMP服务器拉流，提取H.264/AAC数据
 *          知识点：FFmpeg输入流API、RTMP拉流、多线程接收、回调机制
 */

#include "FFmpegRTMPReceiver.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/**
 * @brief 构造函数
 * @details 初始化RTMP接收器对象
 */
FFmpegRTMPReceiver::FFmpegRTMPReceiver()
    : m_bReceiving(false)          // 接收状态：未开始
    , m_bInitialized(false)         // 初始化标志：未初始化
    , m_formatCtx(nullptr)          // FFmpeg格式上下文（输入流）
    , m_videoCodecCtx(nullptr)      // 视频解码器上下文（可选，用于获取流信息）
    , m_audioCodecCtx(nullptr)      // 音频解码器上下文（可选）
    , m_videoStreamIndex(-1)        // 视频流索引（-1=未找到）
    , m_audioStreamIndex(-1)        // 音频流索引（-1=未找到）
    , m_width(0)                    // 视频宽度
    , m_height(0)                   // 视频高度
    , m_fps(25)                     // 帧率
    , m_startTime(0)                // 开始时间戳
    , m_receiveThread(nullptr)      // 接收线程指针
    , m_bThreadRunning(false)       // 线程运行标志
{
}

FFmpegRTMPReceiver::~FFmpegRTMPReceiver()
{
    StopReceive();
}

/**
 * @brief 开始接收RTMP流
 * @details 知识点：FFmpeg输入流初始化流程
 *          1. 分配格式上下文（avformat_alloc_context）
 *          2. 打开输入流（avformat_open_input：连接RTMP服务器）
 *          3. 查找流信息（avformat_find_stream_info：获取视频/音频参数）
 *          4. 查找视频流和音频流索引
 *          5. 启动接收线程
 * @param rtmpUrl RTMP服务器URL（如：rtmp://server/app/stream）
 * @param h264Callback H.264数据回调函数（当接收到视频数据时调用）
 * @param aacCallback AAC数据回调函数（当接收到音频数据时调用）
 * @return true=成功，false=失败
 */
bool FFmpegRTMPReceiver::StartReceive(const std::string& rtmpUrl,
                                     H264DataCallback h264Callback,
                                     AACDataCallback aacCallback)
{
    if (m_bReceiving) {
        printf("Already receiving RTMP stream\n");
        return false;
    }

    // 保存参数
    m_rtmpUrl = rtmpUrl;
    m_h264Callback = h264Callback;
    m_aacCallback = aacCallback;

    // 知识点：分配格式上下文
    // avformat_alloc_context - 分配AVFormatContext结构体
    m_formatCtx = avformat_alloc_context();
    if (!m_formatCtx) {
        printf("Could not allocate format context\n");
        return false;
    }

    // 知识点：设置超时选项
    // AVDictionary - FFmpeg的选项字典
    // rtmp_timeout: RTMP连接超时时间（微秒）
    // stimeout: Socket超时时间
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtmp_timeout", "5000000", 0); // 5秒超时
    av_dict_set(&opts, "stimeout", "5000000", 0);

    // 知识点：打开输入流
    // avformat_open_input - 打开输入流（连接RTMP服务器）
    // 参数：
    //   - &m_formatCtx: 格式上下文（输入输出参数）
    //   - rtmpUrl: RTMP服务器URL
    //   - nullptr: 格式名称（nullptr=自动检测）
    //   - &opts: 选项字典（超时设置等）
    int ret = avformat_open_input(&m_formatCtx, rtmpUrl.c_str(), nullptr, &opts);
    av_dict_free(&opts);  // 释放选项字典

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Could not open input: %s, error: %s\n", rtmpUrl.c_str(), errbuf);
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }

    // 知识点：查找流信息
    // avformat_find_stream_info - 读取流信息（分辨率、帧率、编码器等）
    // 必须调用此函数才能获取流的详细信息
    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        printf("Could not find stream info\n");
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }

    // 知识点：查找视频流
    // 遍历所有流，找到视频流（codec_type == AVMEDIA_TYPE_VIDEO）
    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            
            // 知识点：获取视频信息
            AVStream* videoStream = m_formatCtx->streams[i];
            m_width = videoStream->codecpar->width;   // 视频宽度
            m_height = videoStream->codecpar->height;  // 视频高度
            
            // 知识点：计算帧率
            // avg_frame_rate: 平均帧率（分数形式）
            if (videoStream->avg_frame_rate.num > 0 && videoStream->avg_frame_rate.den > 0) {
                m_fps = videoStream->avg_frame_rate.num / videoStream->avg_frame_rate.den;
            }
            
            printf("Found video stream: %dx%d@%dfps\n", m_width, m_height, m_fps);
            break;
        }
    }

    if (m_videoStreamIndex < 0) {
        printf("Could not find video stream\n");
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }

    // 查找音频流（可选）
    m_audioStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_audioStreamIndex = i;
            printf("Found audio stream\n");
            break;
        }
    }

    m_bInitialized = true;
    m_bReceiving = true;
    m_startTime = av_gettime() / 1000; // 毫秒

    // 启动拉流线程
    m_bThreadRunning = true;
    if (pthread_create(&m_receiveThread, nullptr, 
                       [](void* arg) -> void* {
                           FFmpegRTMPReceiver* receiver = (FFmpegRTMPReceiver*)arg;
                           receiver->ReceiveThreadFunc();
                           return nullptr;
                       }, this) != 0) {
        printf("Failed to create receive thread\n");
        m_bReceiving = false;
        m_bInitialized = false;
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }

    printf("RTMP receiver started: %s\n", rtmpUrl.c_str());

    return true;
}

/**
 * @brief 停止接收RTMP流
 * @details 停止拉流线程，释放所有FFmpeg资源
 *          功能：设置停止标志、等待线程结束、释放解码器上下文、关闭输入流
 *          知识点：线程同步、资源释放、FFmpeg资源清理
 */
void FFmpegRTMPReceiver::StopReceive()
{
    if (!m_bReceiving) {
        return;
    }

    m_bReceiving = false;
    m_bThreadRunning = false;

    // 等待线程结束
    pthread_join(m_receiveThread, nullptr);

    // 释放资源
    if (m_videoCodecCtx) {
        avcodec_free_context(&m_videoCodecCtx);
        m_videoCodecCtx = nullptr;
    }

    if (m_audioCodecCtx) {
        avcodec_free_context(&m_audioCodecCtx);
        m_audioCodecCtx = nullptr;
    }

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    m_bInitialized = false;

    printf("RTMP receiver stopped\n");
}

/**
 * @brief 获取视频信息
 * @details 获取RTMP流的视频参数（分辨率、帧率）
 *          功能：返回视频宽度、高度和帧率，用于配置MP4录制参数
 * @param width 输出视频宽度
 * @param height 输出视频高度
 * @param fps 输出帧率
 */
void FFmpegRTMPReceiver::GetVideoInfo(int& width, int& height, int& fps)
{
    width = m_width;
    height = m_height;
    fps = m_fps;
}

/**
 * @brief 拉流线程函数
 * @details 持续从RTMP流中读取数据包并分发到视频/音频处理函数
 *          功能：循环读取AVPacket、根据流索引分发数据包、处理错误和重连
 *          知识点：FFmpeg数据包读取、多线程拉流、错误处理
 */
void FFmpegRTMPReceiver::ReceiveThreadFunc()
{
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        printf("Could not allocate packet\n");
        m_bReceiving = false;
        return;
    }

    while (m_bReceiving && m_bThreadRunning) {
        int ret = av_read_frame(m_formatCtx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                printf("End of stream\n");
            } else {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                printf("Error reading frame: %s\n", errbuf);
            }
            // 尝试重连
            usleep(1000 * 1000); // 等待1秒后重试
            continue;
        }

        // 处理视频数据包
        if (pkt->stream_index == m_videoStreamIndex) {
            ProcessVideoPacket(pkt);
        }
        // 处理音频数据包
        else if (pkt->stream_index == m_audioStreamIndex && m_aacCallback) {
            ProcessAudioPacket(pkt);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    m_bReceiving = false;
}

/**
 * @brief 处理视频数据包
 * @details 处理H.264视频数据包，提取NAL单元并通过回调函数传递
 *          功能：判断关键帧、计算时间戳、提取NAL单元、调用H.264回调
 *          知识点：AVPacket处理、PTS计算、时间基转换、关键帧判断
 * @param pkt 视频数据包
 */
void FFmpegRTMPReceiver::ProcessVideoPacket(AVPacket* pkt)
{
    if (!m_h264Callback || !pkt || pkt->size <= 0) {
        return;
    }

    // 判断是否为关键帧
    bool isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

    // 计算时间戳（毫秒）
    int64_t pts = 0;
    if (pkt->pts != AV_NOPTS_VALUE) {
        AVStream* stream = m_formatCtx->streams[m_videoStreamIndex];
        pts = av_rescale_q(pkt->pts, stream->time_base, (AVRational){1, 1000});
    } else {
        // 如果没有PTS，使用相对时间
        pts = (av_gettime() / 1000) - m_startTime;
    }

    // 提取NAL单元并回调
    ExtractNALUnits(pkt->data, pkt->size, isKeyFrame, pts);
}

/**
 * @brief 处理音频数据包
 * @details 处理AAC音频数据包，通过回调函数传递给MP4写入器
 *          功能：计算时间戳、调用AAC回调函数传递音频数据
 *          知识点：音频PTS计算、回调机制
 * @param pkt 音频数据包
 */
void FFmpegRTMPReceiver::ProcessAudioPacket(AVPacket* pkt)
{
    if (!m_aacCallback || !pkt || pkt->size <= 0) {
        return;
    }

    // 计算时间戳（毫秒）
    int64_t pts = 0;
    if (pkt->pts != AV_NOPTS_VALUE) {
        AVStream* stream = m_formatCtx->streams[m_audioStreamIndex];
        pts = av_rescale_q(pkt->pts, stream->time_base, (AVRational){1, 1000});
    } else {
        pts = (av_gettime() / 1000) - m_startTime;
    }

    // 回调AAC数据
    m_aacCallback(pkt->data, pkt->size, pts);
}

/**
 * @brief 提取NAL单元
 * @details 从AVCC格式的H.264数据中提取NAL单元，转换为Annex-B格式
 *          功能：解析AVCC格式（4字节长度前缀）、提取每个NAL单元、添加Annex-B起始码、回调数据
 *          知识点：H.264格式转换、AVCC到Annex-B转换、NAL单元解析
 * @param data H.264数据（AVCC格式）
 * @param size 数据大小
 * @param isKeyFrame 是否为关键帧
 * @param pts 时间戳（毫秒）
 */
void FFmpegRTMPReceiver::ExtractNALUnits(uint8_t* data, int size, bool isKeyFrame, int64_t pts)
{
    // RTMP流中的H.264数据通常是AVCC格式（长度前缀），需要转换为Annex-B格式
    // 或者直接提取NAL单元

    int offset = 0;
    while (offset < size) {
        // 读取NAL单元长度（4字节，大端序）
        if (offset + 4 > size) {
            break;
        }

        int nalSize = (data[offset] << 24) | (data[offset + 1] << 16) |
                      (data[offset + 2] << 8) | data[offset + 3];

        if (nalSize <= 0 || offset + 4 + nalSize > size) {
            break;
        }

        // 构造Annex-B格式的NAL单元（添加0x00 0x00 0x00 0x01起始码）
        uint8_t* nalData = data + offset + 4;
        uint8_t startCode[4] = {0x00, 0x00, 0x00, 0x01};
        
        // 分配缓冲区（起始码 + NAL数据）
        uint8_t* annexBData = (uint8_t*)malloc(4 + nalSize);
        if (!annexBData) {
            break;
        }

        memcpy(annexBData, startCode, 4);
        memcpy(annexBData + 4, nalData, nalSize);

        // 回调H.264数据
        if (m_h264Callback) {
            m_h264Callback(annexBData, 4 + nalSize, isKeyFrame, pts);
        }

        free(annexBData);
        offset += 4 + nalSize;
    }
}


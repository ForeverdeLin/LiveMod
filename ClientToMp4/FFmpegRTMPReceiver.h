#ifndef FFMPEGRTMPRECEIVER_H
#define FFMPEGRTMPRECEIVER_H

#include <stdint.h>
#include <string>
#include <functional>
#include <pthread.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

/**
 * @brief H.264数据回调函数类型
 * @param data H.264 NAL单元数据
 * @param size 数据大小
 * @param isKeyFrame 是否为关键帧
 * @param pts 时间戳（毫秒）
 */
typedef std::function<void(uint8_t* data, int size, bool isKeyFrame, int64_t pts)> H264DataCallback;

/**
 * @brief AAC数据回调函数类型
 * @param data AAC数据
 * @param size 数据大小
 * @param pts 时间戳（毫秒）
 */
typedef std::function<void(uint8_t* data, int size, int64_t pts)> AACDataCallback;

/**
 * @brief FFmpeg RTMP拉流器
 * 从RTMP服务器拉取视频流，支持H.264视频和AAC音频
 */
class FFmpegRTMPReceiver
{
public:
    FFmpegRTMPReceiver();
    ~FFmpegRTMPReceiver();

    /**
     * @brief 连接到RTMP服务器并开始拉流
     * @param rtmpUrl RTMP地址（如 rtmp://live.bilibili.com/...）
     * @param h264Callback H.264数据回调函数
     * @param aacCallback AAC数据回调函数（可选）
     * @return 成功返回true
     */
    bool StartReceive(const std::string& rtmpUrl, 
                     H264DataCallback h264Callback,
                     AACDataCallback aacCallback = nullptr);

    /**
     * @brief 停止拉流
     */
    void StopReceive();

    /**
     * @brief 检查是否正在拉流
     */
    bool IsReceiving() const { return m_bReceiving; }

    /**
     * @brief 获取视频信息
     */
    void GetVideoInfo(int& width, int& height, int& fps);

private:
    /**
     * @brief 拉流线程函数
     */
    void ReceiveThreadFunc();

    /**
     * @brief 处理视频数据包
     */
    void ProcessVideoPacket(AVPacket* pkt);

    /**
     * @brief 处理音频数据包
     */
    void ProcessAudioPacket(AVPacket* pkt);

    /**
     * @brief 提取H.264 NAL单元
     */
    void ExtractNALUnits(uint8_t* data, int size, bool isKeyFrame, int64_t pts);

private:
    bool m_bReceiving;                      // 拉流状态
    bool m_bInitialized;                    // 初始化状态
    
    std::string m_rtmpUrl;                  // RTMP地址
    H264DataCallback m_h264Callback;        // H.264回调
    AACDataCallback m_aacCallback;          // AAC回调
    
    AVFormatContext* m_formatCtx;            // 格式上下文
    AVCodecContext* m_videoCodecCtx;        // 视频解码器上下文
    AVCodecContext* m_audioCodecCtx;         // 音频解码器上下文
    
    int m_videoStreamIndex;                 // 视频流索引
    int m_audioStreamIndex;                 // 音频流索引
    
    int m_width;                            // 视频宽度
    int m_height;                           // 视频高度
    int m_fps;                              // 帧率
    
    int64_t m_startTime;                    // 开始时间（毫秒）
    
    // 线程相关
    pthread_t m_receiveThread;              // 拉流线程句柄
    bool m_bThreadRunning;                  // 线程运行状态
};

#endif // FFMPEGRTMPRECEIVER_H


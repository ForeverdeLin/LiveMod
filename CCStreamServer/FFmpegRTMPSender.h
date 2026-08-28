#ifndef FFMPEGRTMPSENDER_H
#define FFMPEGRTMPSENDER_H

#include <stdint.h>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

/**
 * @brief FFmpeg RTMP推流器
 * 使用FFmpeg实现RTMP协议推流，支持多路推流到不同平台
 * 支持H.264/H.265编码，可配置硬件加速
 */
class FFmpegRTMPSender
{
public:
    FFmpegRTMPSender();
    ~FFmpegRTMPSender();

    /**
     * @brief 初始化推流器
     * @param width 视频宽度
     * @param height 视频高度
     * @param fps 帧率
     * @param bitrate 码率（bps）
     * @param rtmpUrls RTMP推流地址列表（支持多平台同时推流）
     * @return 成功返回true
     */
    bool Initialize(int width, int height, int fps, int bitrate, 
                    const std::vector<std::string>& rtmpUrls);

    /**
     * @brief 发送H.264编码后的数据包
     * @param data H.264 NAL单元数据
     * @param size 数据大小
     * @param isKeyFrame 是否为关键帧
     * @param pts 时间戳（毫秒）
     * @return 成功返回true
     */
    bool SendH264Frame(uint8_t* data, int size, bool isKeyFrame, int64_t pts);

    /**
     * @brief 发送AAC音频数据
     * @param data AAC数据
     * @param size 数据大小
     * @param pts 时间戳（毫秒）
     * @return 成功返回true
     */
    bool SendAACFrame(uint8_t* data, int size, int64_t pts);

    /**
     * @brief 关闭推流
     */
    void Close();

    /**
     * @brief 检查推流状态
     * @return 正在推流返回true
     */
    bool IsStreaming() const { return m_bStreaming; }

private:
    /**
     * @brief 初始化单个输出上下文
     */
    bool InitOutputContext(const std::string& rtmpUrl);

    /**
     * @brief 写入视频帧
     */
    bool WriteVideoFrame(AVFormatContext* fmtCtx, uint8_t* data, int size, 
                        bool isKeyFrame, int64_t pts);

    /**
     * @brief 写入音频帧
     */
    bool WriteAudioFrame(AVFormatContext* fmtCtx, uint8_t* data, int size, int64_t pts);

    /**
     * @brief 解析SPS/PPS并设置编码器参数
     */
    bool ParseSPSPPS(uint8_t* data, int size);

private:
    bool m_bStreaming;                    // 推流状态
    bool m_bInitialized;                  // 初始化状态
    
    int m_width;                          // 视频宽度
    int m_height;                         // 视频高度
    int m_fps;                            // 帧率
    int m_bitrate;                        // 码率
    
    std::vector<AVFormatContext*> m_outputContexts;  // 多个输出上下文（多平台推流）格式上下文
    std::vector<std::string> m_rtmpUrls;            // RTMP地址列表
    
    // H.264参数
    uint8_t m_sps[128];                   // SPS数据
    uint8_t m_pps[128];                   // PPS数据
    int m_spsSize;                        // SPS大小
    int m_ppsSize;                        // PPS大小
    bool m_bSpsPpsSent;                   // 是否已发送SPS/PPS
    
    int64_t m_videoPts;                   // 视频时间戳
    int64_t m_audioPts;                   // 音频时间戳
    int64_t m_startTime;                  // 推流开始时间
};

#endif // FFMPEGRTMPSENDER_H


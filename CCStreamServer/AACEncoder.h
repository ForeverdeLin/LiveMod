#ifndef AACENCODER_H
#define AACENCODER_H

#include <stdint.h>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

/**
 * @brief AAC音频编码器
 * 使用FFmpeg的AAC编码器将PCM数据编码为AAC
 */
class AACEncoder
{
public:
    AACEncoder();
    ~AACEncoder();

    /**
     * @brief 初始化编码器
     * @param sampleRate 采样率（如 44100）
     * @param channels 声道数（1或2）
     * @param bitrate 码率（bps，如 128000）
     * @return 成功返回true
     */
    bool Initialize(unsigned int sampleRate = 44100,
                    unsigned int channels = 2,
                    int bitrate = 128000);

    /**
     * @brief 编码PCM数据为AAC
     * @param pcmData PCM输入数据
     * @param pcmSize PCM数据大小（字节）
     * @param aacData AAC输出缓冲区
     * @param aacSize 输出缓冲区大小，返回实际编码的字节数
     * @return 成功返回true
     */
    bool EncodeFrame(const uint8_t* pcmData, int pcmSize,
                     uint8_t* aacData, int* aacSize);

    /**
     * @brief 获取编码器参数
     */
    unsigned int GetSampleRate() const { return m_sampleRate; }
    unsigned int GetChannels() const { return m_channels; }
    int GetBitrate() const { return m_bitrate; }

    /**
     * @brief 反初始化
     */
    void Uninitialize();

private:
    bool m_bInitialized;
    unsigned int m_sampleRate;
    unsigned int m_channels;
    int m_bitrate;

    AVCodecContext* m_codecContext;
    AVFrame* m_frame;
    AVPacket* m_packet;
    SwrContext* m_swrContext;  // 格式转换上下文

    int m_frameSize;  // 每帧样本数
};

#endif // AACENCODER_H


#ifndef AACDECODER_H
#define AACDECODER_H

#include <stdint.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

/**
 * @brief AAC解码器
 * 使用FFmpeg解码AAC音频为PCM
 */
class AACDecoder
{
public:
    AACDecoder();
    ~AACDecoder();

    /**
     * @brief 初始化解码器
     * @param sampleRate 采样率（默认44100）
     * @param channels 声道数（默认2）
     * @return 成功返回true
     */
    bool Initialize(int sampleRate = 44100, int channels = 2);

    /**
     * @brief 解码AAC数据
     * @param aacData AAC数据
     * @param aacSize AAC数据大小
     * @param pcmData 输出的PCM数据缓冲区（需要调用者分配足够空间）
     * @param pcmSize 输入：缓冲区大小，输出：实际PCM数据大小
     * @return 成功返回true
     */
    bool DecodeAAC(uint8_t* aacData, int aacSize, uint8_t* pcmData, int* pcmSize);

    /**
     * @brief 反初始化
     */
    void Uninitialize();

    /**
     * @brief 获取采样率
     */
    int GetSampleRate() const { return m_sampleRate; }

    /**
     * @brief 获取声道数
     */
    int GetChannels() const { return m_channels; }

    /**
     * @brief 获取每个样本的字节数
     */
    int GetBytesPerSample() const { return 2; } // S16格式，2字节/样本

private:
    bool m_bInitialized;
    int m_sampleRate;
    int m_channels;
    
    AVCodecContext* m_codecCtx;
    AVCodec* m_codec;
    AVPacket* m_packet;
    AVFrame* m_frame;
};

#endif // AACDECODER_H


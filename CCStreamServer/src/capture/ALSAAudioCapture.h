#ifndef ALSAAUDIOCAPTURE_H
#define ALSAAUDIOCAPTURE_H

#include <stdint.h>
#include <alsa/asoundlib.h>
#include <string>

/**
 * @brief ALSA音频采集模块
 * 使用ALSA库采集PCM音频数据
 */
class ALSAAudioCapture
{
public:
    ALSAAudioCapture();
    ~ALSAAudioCapture();

    /**
     * @brief 初始化音频采集
     * @param deviceName ALSA设备名称（如 "default", "hw:0,0"）
     * @param sampleRate 采样率（如 44100, 48000）
     * @param channels 声道数（1=单声道, 2=立体声）
     * @param format 采样格式（S16_LE, S32_LE等）
     * @return 成功返回true
     */
    bool Initialize(const std::string& deviceName = "default",
                    unsigned int sampleRate = 44100,
                    unsigned int channels = 2,
                    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE);

    /**
     * @brief 开始采集
     * @return 成功返回true
     */
    bool StartCapture();

    /**
     * @brief 停止采集
     */
    void StopCapture();

    /**
     * @brief 读取一帧PCM数据
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小（字节）
     * @return 实际读取的字节数，失败返回-1
     */
    int ReadFrame(uint8_t* buffer, int bufferSize);

    /**
     * @brief 获取采样率
     */
    unsigned int GetSampleRate() const { return m_sampleRate; }

    /**
     * @brief 获取声道数
     */
    unsigned int GetChannels() const { return m_channels; }

    /**
     * @brief 获取采样格式
     */
    snd_pcm_format_t GetFormat() const { return m_format; }

    /**
     * @brief 获取每帧样本数
     */
    unsigned int GetFramesPerPeriod() const { return m_framesPerPeriod; }

    /**
     * @brief 检查是否正在采集
     */
    bool IsCapturing() const { return m_bCapturing; }

    /**
     * @brief 反初始化
     */
    void Uninitialize();

private:
    bool m_bInitialized;
    bool m_bCapturing;
    
    snd_pcm_t* m_pcmHandle;
    std::string m_deviceName;
    unsigned int m_sampleRate;
    unsigned int m_channels;
    snd_pcm_format_t m_format;
    unsigned int m_framesPerPeriod;
    unsigned int m_periods;
    
    uint8_t* m_pcmBuffer;
    int m_pcmBufferSize;
};

#endif // ALSAAUDIOCAPTURE_H


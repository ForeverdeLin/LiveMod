/**
 * @file aacdecoder.cpp
 * @brief AAC音频解码器实现
 * @details 使用FFmpeg库将AAC编码数据解码为PCM格式
 *          知识点：FFmpeg音频解码器API、AAC解码流程、PCM格式处理
 */

#include "aacdecoder.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 构造函数
 * @details 初始化AAC解码器对象
 */
AACDecoder::AACDecoder()
    : m_bInitialized(false)      // 初始化标志：未初始化
    , m_sampleRate(44100)        // 默认采样率：44.1kHz
    , m_channels(2)              // 默认声道数：立体声
    , m_codecCtx(nullptr)        // FFmpeg解码器上下文
    , m_codec(nullptr)           // FFmpeg解码器
    , m_packet(nullptr)          // AVPacket（编码数据）
    , m_frame(nullptr)           // AVFrame（解码后的PCM数据）
{
}

AACDecoder::~AACDecoder()
{
    Uninitialize();
}

/**
 * @brief 初始化AAC解码器
 * @details 知识点：FFmpeg音频解码器初始化流程
 *          1. 查找解码器（avcodec_find_decoder）
 *          2. 分配解码器上下文（avcodec_alloc_context3）
 *          3. 配置解码器参数（采样率、声道、格式等）
 *          4. 打开解码器（avcodec_open2）
 *          5. 分配数据包和帧缓冲区
 * @param sampleRate 采样率（Hz），常用值：44100、48000
 * @param channels 声道数，1=单声道，2=立体声
 * @return true=成功，false=失败
 */
bool AACDecoder::Initialize(int sampleRate, int channels)
{
    // 如果已初始化，先反初始化
    if (m_bInitialized) {
        Uninitialize();
    }

    m_sampleRate = sampleRate;
    m_channels = channels;

    // 知识点：查找AAC解码器
    // avcodec_find_decoder - 根据编码器ID查找可用的解码器
    m_codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!m_codec) {
        printf("AAC decoder not found\n");
        return false;
    }

    // 知识点：分配解码器上下文
    // avcodec_alloc_context3 - 为解码器分配上下文结构体
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
        printf("Could not allocate audio codec context\n");
        return false;
    }

    // 知识点：配置解码器参数
    m_codecCtx->sample_rate = m_sampleRate;                              // 采样率
    m_codecCtx->channels = m_channels;                                   // 声道数
    m_codecCtx->channel_layout = av_get_default_channel_layout(m_channels); // 声道布局
    m_codecCtx->sample_fmt = AV_SAMPLE_FMT_S16;                          // 输出格式：16位有符号整数
    m_codecCtx->bit_rate = 0;                                             // 码率（解码时不需要）
    m_codecCtx->time_base.num = 1;                                        // 时间基分子
    m_codecCtx->time_base.den = m_sampleRate;                            // 时间基分母（采样率）

    // 知识点：打开解码器
    // avcodec_open2 - 使用配置的参数打开解码器
    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        printf("Could not open codec\n");
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    // 知识点：分配数据包和帧缓冲区
    // av_packet_alloc - 分配AVPacket（用于存储编码数据）
    // av_frame_alloc - 分配AVFrame（用于存储解码后的PCM数据）
    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    if (!m_packet || !m_frame) {
        printf("Could not allocate packet or frame\n");
        Uninitialize();
        return false;
    }

    m_bInitialized = true;
    printf("AAC decoder initialized: %dHz, %d channels\n", m_sampleRate, m_channels);
    return true;
}

/**
 * @brief 解码AAC数据为PCM
 * @details 知识点：FFmpeg音频解码流程
 *          1. 准备AVPacket（封装AAC数据）
 *          2. 发送数据包到解码器（avcodec_send_packet）
 *          3. 接收解码后的帧（avcodec_receive_frame）
 *          4. 提取PCM数据（交错格式S16）
 * @param aacData 输入的AAC编码数据
 * @param aacSize AAC数据大小（字节）
 * @param pcmData 输出的PCM数据缓冲区（调用者分配）
 * @param pcmSize 输入输出参数：输入=缓冲区大小，输出=实际PCM数据大小
 * @return true=成功，false=失败或需要更多数据
 */
bool AACDecoder::DecodeAAC(uint8_t* aacData, int aacSize, uint8_t* pcmData, int* pcmSize)
{
    if (!m_bInitialized || !aacData || !pcmData || !pcmSize) {
        return false;
    }

    // 知识点：准备数据包
    // av_packet_unref - 释放数据包的引用（清空之前的数据）
    av_packet_unref(m_packet);
    m_packet->data = aacData;  // 设置AAC数据指针
    m_packet->size = aacSize;  // 设置数据大小

    // 知识点：发送数据到解码器
    // avcodec_send_packet - 将AVPacket发送到解码器
    int ret = avcodec_send_packet(m_codecCtx, m_packet);
    if (ret < 0) {
        printf("Error sending packet to decoder: %d\n", ret);
        return false;
    }

    // 知识点：接收解码后的帧
    // avcodec_receive_frame - 从解码器接收解码后的音频帧
    ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret < 0) {
        // AVERROR(EAGAIN): 需要更多输入数据（正常情况，不是错误）
        // AVERROR_EOF: 输入数据已耗尽（正常情况）
        return false;
    }

    // 知识点：计算PCM数据大小
    // nb_samples: 帧中的样本数
    int samples = m_frame->nb_samples;
    int channels = m_frame->channels;
    int bytesPerSample = av_get_bytes_per_sample((AVSampleFormat)m_frame->format);
    int dataSize = samples * channels * bytesPerSample;

    if (dataSize > *pcmSize) {
        printf("PCM buffer too small: need %d, got %d\n", dataSize, *pcmSize);
        return false;
    }

    // 复制PCM数据（如果是planar格式需要交错）
    if (av_sample_fmt_is_planar((AVSampleFormat)m_frame->format)) {
        // Planar格式：每个声道单独存储
        int sampleSize = bytesPerSample;
        for (int i = 0; i < samples; i++) {
            for (int ch = 0; ch < channels; ch++) {
                memcpy(pcmData + (i * channels + ch) * sampleSize,
                       m_frame->data[ch] + i * sampleSize,
                       sampleSize);
            }
        }
    } else {
        // Packed格式：直接复制
        memcpy(pcmData, m_frame->data[0], dataSize);
    }

    *pcmSize = dataSize;
    return true;
}

void AACDecoder::Uninitialize()
{
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }

    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    m_bInitialized = false;
}


/**
 * @file AACEncoder.cpp
 * @brief AAC音频编码器实现
 * @details 使用FFmpeg库实现PCM到AAC的音频编码
 *          知识点：FFmpeg编码器API、音频格式转换（swresample）、AVFrame/AVPacket使用
 */

#include "AACEncoder.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libavutil/samplefmt.h>

/**
 * @brief 构造函数
 * @details 初始化所有成员变量为默认值
 *          默认参数：采样率44100Hz，2声道，码率128kbps
 */
AACEncoder::AACEncoder()
    : m_bInitialized(false)      // 初始化标志：未初始化
    , m_sampleRate(44100)        // 默认采样率：44.1kHz（CD质量）
    , m_channels(2)               // 默认声道数：立体声
    , m_bitrate(128000)           // 默认码率：128kbps
    , m_codecContext(nullptr)     // FFmpeg编码器上下文（用于配置编码参数）
    , m_frame(nullptr)            // AVFrame对象（存储待编码的音频帧）
    , m_packet(nullptr)           // AVPacket对象（存储编码后的数据包）
    , m_swrContext(nullptr)       // SwrContext对象（用于PCM格式转换：S16->FLTP）
    , m_frameSize(0)              // 每帧样本数（由编码器决定）
{
}

/**
 * @brief 析构函数
 * @details 自动清理所有FFmpeg资源，防止内存泄漏
 */
AACEncoder::~AACEncoder()
{
    Uninitialize();
}

/**
 * @brief 初始化AAC编码器
 * @details 知识点：FFmpeg编码器初始化流程
 *          1. 查找编码器（avcodec_find_encoder）
 *          2. 分配编码器上下文（avcodec_alloc_context3）
 *          3. 配置编码参数（采样率、声道、格式等）
 *          4. 打开编码器（avcodec_open2）
 *          5. 创建格式转换器（swresample，将S16转为FLTP）
 *          6. 分配帧和包缓冲区
 * @param sampleRate 采样率（Hz），常用值：44100、48000
 * @param channels 声道数，1=单声道，2=立体声
 * @param bitrate 码率（bps），常用值：64000、128000、192000
 * @return true=成功，false=失败
 */
bool AACEncoder::Initialize(unsigned int sampleRate,
                            unsigned int channels,
                            int bitrate)
{
    // 防止重复初始化
    if (m_bInitialized) {
        printf("AACEncoder already initialized\n");
        return false;
    }

    // 保存编码参数
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_bitrate = bitrate;

    // 知识点：FFmpeg编码器查找
    // avcodec_find_encoder - 根据编码器ID查找可用的编码器
    // AV_CODEC_ID_AAC - AAC编码器的标识符
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        printf("AAC encoder not found\n");
        return false;
    }

    // 知识点：FFmpeg上下文分配
    // avcodec_alloc_context3 - 为编码器分配上下文结构体
    // 上下文用于存储编码器的配置参数和状态
    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        printf("Failed to allocate codec context\n");
        return false;
    }

    // 知识点：编码器参数配置
    // 设置编码器的各项参数，这些参数决定了输出AAC的质量和格式
    m_codecContext->bit_rate = m_bitrate;              // 码率：影响文件大小和音质
    m_codecContext->sample_rate = m_sampleRate;        // 采样率：必须与输入PCM一致
    m_codecContext->channels = m_channels;              // 声道数
    m_codecContext->channel_layout = (m_channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;  // 声道布局
    m_codecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;   // 采样格式：FLTP（浮点平面格式），AAC编码器要求
    m_codecContext->profile = FF_PROFILE_AAC_LOW;      // 编码配置文件：AAC-LC（低复杂度）

    // 知识点：打开编码器
    // avcodec_open2 - 使用配置的参数打开编码器，进行实际初始化
    // 第三个参数为AVDictionary*，用于传递额外的编码选项（这里传nullptr使用默认值）
    int ret = avcodec_open2(m_codecContext, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));  // 将错误码转换为可读字符串
        printf("Failed to open codec: %s\n", errbuf);
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
        return false;
    }

    // 获取编码器要求的帧大小（每帧样本数）
    // 不同编码器对帧大小有不同要求，AAC通常是1024或2048
    m_frameSize = m_codecContext->frame_size;
    if (m_frameSize <= 0) {
        m_frameSize = 1024;  // 默认值：1024样本/帧
    }

    // 知识点：音频格式转换器（swresample）
    // 问题：ALSA采集的是S16格式（16位有符号整数），但AAC编码器需要FLTP格式（32位浮点平面）
    // 解决：使用swresample库进行格式转换
    // swr_alloc - 分配格式转换上下文
    m_swrContext = swr_alloc();
    if (!m_swrContext) {
        printf("Failed to allocate swresample context\n");
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
        return false;
    }

    // 知识点：配置格式转换器的输入格式（ALSA采集的PCM格式）
    // av_opt_set_int - 设置选项参数（整数类型）
    // in_channel_layout - 输入声道布局（单声道/立体声）
    // in_sample_rate - 输入采样率
    // in_sample_fmt - 输入采样格式：AV_SAMPLE_FMT_S16（16位有符号整数交错格式）
    av_opt_set_int(m_swrContext, "in_channel_layout", 
                   (m_channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(m_swrContext, "in_sample_rate", m_sampleRate, 0);
    av_opt_set_sample_fmt(m_swrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    // 知识点：配置格式转换器的输出格式（AAC编码器需要的格式）
    // out_sample_fmt - 输出采样格式：AV_SAMPLE_FMT_FLTP（32位浮点平面格式）
    // 平面格式：每个声道的数据单独存储在一个数组中（而非交错存储）
    av_opt_set_int(m_swrContext, "out_channel_layout", m_codecContext->channel_layout, 0);
    av_opt_set_int(m_swrContext, "out_sample_rate", m_sampleRate, 0);
    av_opt_set_sample_fmt(m_swrContext, "out_sample_fmt", m_codecContext->sample_fmt, 0);

    // 知识点：初始化格式转换器
    // swr_init - 根据配置的参数初始化转换器，分配内部缓冲区
    ret = swr_init(m_swrContext);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Failed to initialize swresample: %s\n", errbuf);
        swr_free(&m_swrContext);
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
        return false;
    }

    // 知识点：分配AVFrame（音频帧）
    // AVFrame用于存储一帧音频数据（转换后的FLTP格式）
    // av_frame_alloc - 分配AVFrame结构体（但不分配数据缓冲区）
    m_frame = av_frame_alloc();
    if (!m_frame) {
        printf("Failed to allocate frame\n");
        swr_free(&m_swrContext);
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
        return false;
    }

    // 配置帧的参数（必须与编码器上下文一致）
    m_frame->nb_samples = m_frameSize;                      // 每帧样本数
    m_frame->format = m_codecContext->sample_fmt;           // 采样格式：FLTP
    m_frame->channel_layout = m_codecContext->channel_layout; // 声道布局
    m_frame->channels = m_channels;                          // 声道数
    m_frame->sample_rate = m_sampleRate;                     // 采样率

    // 知识点：为AVFrame分配数据缓冲区
    // av_frame_get_buffer - 根据帧的参数分配实际的数据缓冲区
    // 参数2：对齐方式（0=自动对齐，通常用于优化内存访问）
    ret = av_frame_get_buffer(m_frame, 0);
    if (ret < 0) {
        printf("Failed to allocate frame buffers\n");
        av_frame_free(&m_frame);
        swr_free(&m_swrContext);
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
        return false;
    }

    // 知识点：分配AVPacket（数据包）
    // AVPacket用于存储编码后的AAC数据
    // av_packet_alloc - 分配AVPacket结构体
    m_packet = av_packet_alloc();
    if (!m_packet) {
        printf("Failed to allocate packet\n");
        av_frame_free(&m_frame);
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
        return false;
    }

    m_bInitialized = true;
    printf("AACEncoder initialized: rate=%u, channels=%u, bitrate=%d, frame_size=%d\n",
           m_sampleRate, m_channels, m_bitrate, m_frameSize);

    return true;
}

/**
 * @brief 编码一帧PCM数据为AAC
 * @details 知识点：FFmpeg编码流程
 *          1. 格式转换：S16 -> FLTP（使用swresample）
 *          2. 发送帧到编码器：avcodec_send_frame
 *          3. 接收编码后的包：avcodec_receive_packet
 * @param pcmData 输入的PCM数据（S16格式，来自ALSA采集）
 * @param pcmSize PCM数据大小（字节）
 * @param aacData 输出的AAC数据缓冲区（调用者分配）
 * @param aacSize 输入：缓冲区大小；输出：实际编码的字节数
 * @return true=成功（aacSize>0表示有数据，=0表示需要更多输入），false=失败
 */
bool AACEncoder::EncodeFrame(const uint8_t* pcmData, int pcmSize,
                             uint8_t* aacData, int* aacSize)
{
    // 参数校验
    if (!m_bInitialized || !pcmData || !aacData || !aacSize) {
        return false;
    }

    // 知识点：计算期望的输入数据大小
    // S16格式：每个样本2字节
    // 期望大小 = 帧大小 × 声道数 × 每样本字节数
    int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);  // 返回2
    int expectedSize = m_frameSize * m_channels * bytesPerSample;
    if (pcmSize < expectedSize) {
        printf("PCM data too small: need %d, got %d\n", expectedSize, pcmSize);
        return false;
    }

    // 知识点：确保帧可写
    // av_frame_make_writable - 如果帧的缓冲区被引用，则创建新的缓冲区
    // 这里确保m_frame可以安全写入转换后的数据
    if (av_frame_make_writable(m_frame) < 0) {
        printf("Frame not writable\n");
        return false;
    }

    // 计算输入样本数
    // S16格式：每个样本2字节
    // 样本数 = 总字节数 / (声道数 × 每样本字节数)
    int inputSamples = pcmSize / (m_channels * 2);
    
    // 知识点：音频格式转换（swresample）
    // swr_convert - 将输入音频数据转换为输出格式
    // 参数：
    //   - m_swrContext: 转换器上下文
    //   - m_frame->data: 输出缓冲区（FLTP格式，平面布局）
    //   - m_frame->nb_samples: 输出缓冲区可容纳的最大样本数
    //   - inData: 输入数据指针数组（S16格式，交错布局）
    //   - inputSamples: 输入样本数
    // 返回：实际转换的样本数（可能小于输入，因为需要缓冲）
    const uint8_t* inData[1] = { pcmData };  // 输入数据指针数组（单声道时只有1个指针）
    int inLinesize[1] = { pcmSize };         // 输入数据每行的字节数
    
    int outSamples = swr_convert(m_swrContext, 
                                 m_frame->data, m_frame->nb_samples,
                                 inData, inputSamples);
    
    if (outSamples < 0) {
        printf("Error converting audio format\n");
        return false;
    }
    
    // 如果转换的样本数为0，说明转换器需要更多输入数据才能输出
    // 这是正常情况，不是错误
    if (outSamples == 0) {
        *aacSize = 0;
        return true;
    }
    
    // 更新帧的实际样本数
    m_frame->nb_samples = outSamples;

    // 知识点：FFmpeg编码流程 - 发送帧到编码器
    // avcodec_send_frame - 将一帧音频数据发送到编码器
    // 编码器内部会缓冲数据，可能不会立即输出编码结果
    // 返回：0=成功，<0=错误
    int ret = avcodec_send_frame(m_codecContext, m_frame);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Error sending frame: %s\n", errbuf);
        return false;
    }

    // 知识点：FFmpeg编码流程 - 接收编码后的数据包
    // avcodec_receive_packet - 从编码器接收编码后的数据包
    // 注意：发送一帧可能产生0个或多个包，也可能需要发送多帧才能产生一个包
    // 返回：
    //   0 = 成功，收到一个包
    //   AVERROR(EAGAIN) = 需要更多输入帧
    //   AVERROR_EOF = 编码器已刷新，没有更多数据
    //   其他负值 = 错误
    ret = avcodec_receive_packet(m_codecContext, m_packet);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // AVERROR(EAGAIN)：编码器需要更多输入才能产生输出（正常情况）
            // AVERROR_EOF：编码器已结束（正常情况，在刷新时）
            *aacSize = 0;
            return true;
        }
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Error receiving packet: %s\n", errbuf);
        return false;
    }

    // 检查输出缓冲区大小是否足够
    if (*aacSize < m_packet->size) {
        printf("AAC buffer too small: need %d, got %d\n", m_packet->size, *aacSize);
        av_packet_unref(m_packet);  // 释放包的引用
        return false;
    }

    // 复制编码后的AAC数据到用户提供的缓冲区
    // m_packet->data: 编码后的AAC数据
    // m_packet->size: 数据大小（字节）
    memcpy(aacData, m_packet->data, m_packet->size);
    *aacSize = m_packet->size;  // 返回实际编码的字节数

    // 知识点：释放数据包引用
    // av_packet_unref - 减少包的引用计数，如果为0则释放内存
    // 注意：这里只是释放引用，m_packet结构体本身不释放（可重复使用）
    av_packet_unref(m_packet);
    return true;
}

/**
 * @brief 反初始化编码器
 * @details 释放所有FFmpeg资源，防止内存泄漏
 *          知识点：FFmpeg资源释放顺序（先释放依赖的资源）
 */
void AACEncoder::Uninitialize()
{
    // 知识点：释放格式转换器
    // swr_free - 释放SwrContext及其内部缓冲区
    if (m_swrContext) {
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
    }

    // 知识点：释放数据包
    // av_packet_free - 释放AVPacket结构体和数据缓冲区
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }

    // 知识点：释放音频帧
    // av_frame_free - 释放AVFrame结构体和数据缓冲区
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    // 知识点：释放编码器上下文
    // avcodec_free_context - 释放AVCodecContext及其内部资源
    // 注意：这会自动关闭编码器
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    m_bInitialized = false;
    printf("AACEncoder uninitialized\n");
}


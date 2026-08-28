/**
 * @file ALSAAudioCapture.cpp
 * @brief ALSA音频采集模块实现
 * @details 使用ALSA库从Linux系统音频设备采集PCM音频数据
 *          知识点：ALSA库API、PCM设备操作、音频硬件参数配置
 */

#include "ALSAAudioCapture.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief 构造函数
 * @details 初始化所有成员变量为默认值
 *          默认参数：设备"default"，采样率44100Hz，2声道，S16_LE格式
 */
ALSAAudioCapture::ALSAAudioCapture()
    : m_bInitialized(false)              // 初始化标志：未初始化
    , m_bCapturing(false)                // 采集状态：未开始
    , m_pcmHandle(nullptr)                // ALSA PCM设备句柄
    , m_deviceName("default")            // 默认设备名（ALSA自动选择）
    , m_sampleRate(44100)                // 默认采样率：44.1kHz
    , m_channels(2)                      // 默认声道数：立体声
    , m_format(SND_PCM_FORMAT_S16_LE)    // 默认格式：16位有符号整数，小端序
    , m_framesPerPeriod(1024)            // 每周期帧数（缓冲区大小）
    , m_periods(4)                       // 周期数（总缓冲区 = periods × framesPerPeriod）
    , m_pcmBuffer(nullptr)               // PCM数据缓冲区
    , m_pcmBufferSize(0)                 // 缓冲区大小（字节）
{
}

/**
 * @brief 析构函数
 * @details 自动清理ALSA资源，防止内存泄漏
 */
ALSAAudioCapture::~ALSAAudioCapture()
{
    Uninitialize();
}

/**
 * @brief 初始化音频采集设备
 * @details 知识点：ALSA设备初始化流程
 *          1. 打开PCM设备（snd_pcm_open）
 *          2. 分配硬件参数结构（snd_pcm_hw_params_alloca）
 *          3. 初始化硬件参数（snd_pcm_hw_params_any）
 *          4. 设置各项参数（访问模式、格式、声道、采样率、周期等）
 *          5. 应用参数（snd_pcm_hw_params）
 *          6. 分配数据缓冲区
 * @param deviceName ALSA设备名称
 *                  "default" = 使用默认设备（推荐）
 *                  "hw:0,0" = 直接使用硬件设备0的子设备0
 *                  "plughw:0,0" = 使用插件层，自动处理格式转换
 * @param sampleRate 采样率（Hz），常用值：44100、48000
 * @param channels 声道数，1=单声道，2=立体声
 * @param format 采样格式
 *               SND_PCM_FORMAT_S16_LE = 16位有符号整数，小端序（最常用）
 *               SND_PCM_FORMAT_S32_LE = 32位有符号整数，小端序
 * @return true=成功，false=失败
 */
bool ALSAAudioCapture::Initialize(const std::string& deviceName,
                                   unsigned int sampleRate,
                                   unsigned int channels,
                                   snd_pcm_format_t format)
{
    // 防止重复初始化
    if (m_bInitialized) {
        printf("ALSAAudioCapture already initialized\n");
        return false;
    }

    // 保存配置参数
    m_deviceName = deviceName;
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_format = format;
    m_framesPerPeriod = 1024; // 默认每周期1024帧（约23ms @ 44.1kHz）

    // 知识点：打开PCM设备
    // snd_pcm_open - 打开ALSA PCM设备
    // 参数：
    //   - &m_pcmHandle: 输出参数，返回设备句柄
    //   - deviceName: 设备名称字符串
    //   - SND_PCM_STREAM_CAPTURE: 打开为采集模式（录音）
    //   - 0: 打开模式标志（0=阻塞模式）
    // 返回：0=成功，<0=错误码
    int ret = snd_pcm_open(&m_pcmHandle, m_deviceName.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (ret < 0) {
        printf("Failed to open PCM device %s: %s\n", m_deviceName.c_str(), snd_strerror(ret));
        return false;
    }

    // 知识点：分配硬件参数结构
    // snd_pcm_hw_params_alloca - 在栈上分配硬件参数结构（自动释放）
    // 注意：alloca在栈上分配，函数返回时自动释放，无需手动free
    snd_pcm_hw_params_t* hwParams = nullptr;
    snd_pcm_hw_params_alloca(&hwParams);

    // 知识点：初始化硬件参数
    // snd_pcm_hw_params_any - 用设备支持的所有可能值填充参数结构
    // 这是设置参数的起点，后续调用set函数会限制这些值
    ret = snd_pcm_hw_params_any(m_pcmHandle, hwParams);
    if (ret < 0) {
        printf("Failed to initialize hardware parameters: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }

    // 知识点：设置访问模式
    // SND_PCM_ACCESS_RW_INTERLEAVED = 交错模式（最常用）
    //   数据格式：L R L R L R ...（左右声道交替存储）
    // 其他模式：MMAP（内存映射，性能更好但更复杂）
    ret = snd_pcm_hw_params_set_access(m_pcmHandle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ret < 0) {
        printf("Failed to set access mode: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }

    // 知识点：设置采样格式
    // 指定PCM数据的位深度和字节序
    // S16_LE = 16位有符号整数，小端序（Intel x86架构）
    ret = snd_pcm_hw_params_set_format(m_pcmHandle, hwParams, m_format);
    if (ret < 0) {
        printf("Failed to set format: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }

    // 知识点：设置声道数
    // 1 = 单声道（Mono）
    // 2 = 立体声（Stereo）
    ret = snd_pcm_hw_params_set_channels(m_pcmHandle, hwParams, m_channels);
    if (ret < 0) {
        printf("Failed to set channels: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }

    // 知识点：设置采样率
    // snd_pcm_hw_params_set_rate_near - 设置最接近指定值的采样率
    // 因为硬件可能不支持精确的采样率，所以使用"near"函数
    // actualRate: 输入输出参数，输入期望值，输出实际设置的值
    unsigned int actualRate = m_sampleRate;
    ret = snd_pcm_hw_params_set_rate_near(m_pcmHandle, hwParams, &actualRate, 0);
    if (ret < 0) {
        printf("Failed to set sample rate: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }
    // 如果实际采样率与期望不同，更新成员变量
    if (actualRate != m_sampleRate) {
        printf("Warning: sample rate %u not available, using %u\n", m_sampleRate, actualRate);
        m_sampleRate = actualRate;
    }

    // 知识点：设置周期大小（Period Size）
    // 周期：ALSA缓冲区被分成多个周期，每次读取一个周期的数据
    // 周期大小影响延迟：周期越小，延迟越低，但CPU占用越高
    // 1024帧 @ 44.1kHz ≈ 23ms延迟
    snd_pcm_uframes_t frames = m_framesPerPeriod;
    ret = snd_pcm_hw_params_set_period_size_near(m_pcmHandle, hwParams, &frames, 0);
    if (ret < 0) {
        printf("Failed to set period size: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }
    m_framesPerPeriod = frames;  // 更新为实际设置的值

    // 知识点：设置周期数（Periods）
    // 周期数：总缓冲区包含多少个周期
    // 总缓冲区大小 = periods × framesPerPeriod
    // 更多周期 = 更大的缓冲区 = 更低的欠载风险，但更高的延迟
    ret = snd_pcm_hw_params_set_periods(m_pcmHandle, hwParams, m_periods, 0);
    if (ret < 0) {
        printf("Failed to set periods: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }

    // 知识点：应用硬件参数
    // snd_pcm_hw_params - 将配置好的参数应用到PCM设备
    // 只有调用此函数，之前的set操作才会真正生效
    ret = snd_pcm_hw_params(m_pcmHandle, hwParams);
    if (ret < 0) {
        printf("Failed to set hardware parameters: %s\n", snd_strerror(ret));
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }

    // 知识点：分配PCM数据缓冲区
    // 计算缓冲区大小：周期大小 × 声道数 × 每样本字节数
    // snd_pcm_format_width - 获取格式的位宽度（如S16返回16）
    int bytesPerSample = snd_pcm_format_width(m_format) / 8;  // 位转字节
    m_pcmBufferSize = m_framesPerPeriod * m_channels * bytesPerSample;
    m_pcmBuffer = (uint8_t*)malloc(m_pcmBufferSize);
    if (!m_pcmBuffer) {
        printf("Failed to allocate PCM buffer\n");
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
        return false;
    }

    m_bInitialized = true;
    printf("ALSAAudioCapture initialized: device=%s, rate=%u, channels=%u, format=%d\n",
           m_deviceName.c_str(), m_sampleRate, m_channels, (int)m_format);

    return true;
}



/**
 * @brief 开始音频采集
 * @details 知识点：ALSA设备准备
 *          snd_pcm_prepare - 准备PCM设备，使其进入可读取状态
 *          必须在每次开始采集前调用，或在出错恢复后调用
 * @return true=成功，false=失败
 */
bool ALSAAudioCapture::StartCapture()
{
    // 检查初始化状态
    if (!m_bInitialized || !m_pcmHandle) {
        printf("ALSAAudioCapture not initialized\n");
        return false;
    }

    // 如果已经在采集，直接返回成功
    if (m_bCapturing) {
        printf("ALSAAudioCapture already capturing\n");
        return true;
    }

    // 知识点：准备PCM设备
    // snd_pcm_prepare - 将PCM设备置于准备状态，可以开始读取数据
    // 必须在设置参数后、读取数据前调用
    // 如果设备处于错误状态（如欠载），此函数会重置设备
    int ret = snd_pcm_prepare(m_pcmHandle);
    if (ret < 0) {
        printf("Failed to prepare PCM: %s\n", snd_strerror(ret));
        return false;
    }

    m_bCapturing = true;
    printf("ALSAAudioCapture started\n");
    return true;
}

/**
 * @brief 停止音频采集
 * @details 知识点：ALSA设备停止
 *          snd_pcm_drop - 立即停止采集，丢弃缓冲区中的数据
 *          与snd_pcm_drain不同：drain会等待缓冲区数据读取完毕，drop立即停止
 */
void ALSAAudioCapture::StopCapture()
{
    if (!m_bCapturing) {
        return;
    }

    m_bCapturing = false;
    
    // 知识点：停止PCM设备
    // snd_pcm_drop - 立即停止采集，丢弃所有未读取的数据
    // 用于快速停止，不等待缓冲区清空
    if (m_pcmHandle) {
        snd_pcm_drop(m_pcmHandle);
    }

    printf("ALSAAudioCapture stopped\n");
}






/**在初始化和开启采集之后
 * @brief 读取一帧PCM音频数据
 * @details 知识点：ALSA数据读取
 *          snd_pcm_readi - 从PCM设备读取交错格式的音频数据
 *          错误处理：EPIPE表示欠载（underrun），需要恢复设备
 * @param buffer 输出缓冲区（调用者分配）
 * @param bufferSize 缓冲区大小（字节），必须 >= m_pcmBufferSize
 * @return 实际读取的字节数，0=无数据，-1=错误
 */
int ALSAAudioCapture::ReadFrame(uint8_t* buffer, int bufferSize)
{
    // 参数校验
    if (!m_bCapturing || !m_pcmHandle || !buffer) {
        return -1;
    }

    // 检查缓冲区大小
    if (bufferSize < m_pcmBufferSize) {
        printf("Buffer too small: need %d, got %d\n", m_pcmBufferSize, bufferSize);
        return -1;
    }

    // 知识点：从PCM设备读取数据
    // snd_pcm_readi - 读取交错格式（interleaved）的音频数据
    // 参数：
    //   - m_pcmHandle: PCM设备句柄
    //   - m_pcmBuffer: 数据缓冲区
    //   - m_framesPerPeriod: 要读取的帧数
    // 返回：实际读取的帧数，<0=错误码
    snd_pcm_sframes_t frames = snd_pcm_readi(m_pcmHandle, m_pcmBuffer, m_framesPerPeriod);
    if (frames < 0) {
        // 知识点：错误处理
        // EPIPE = 欠载（underrun）：应用程序读取太慢，硬件缓冲区空了
        // 解决方法：调用snd_pcm_prepare重置设备状态
        if (frames == -EPIPE) {
            printf("ALSA underrun, recovering...\n");
            snd_pcm_prepare(m_pcmHandle);  // 恢复设备
            return 0;  // 本次读取失败，返回0
        } else {
            printf("Failed to read from PCM: %s\n", snd_strerror(frames));
            return -1;
        }
    }

    // 如果读取到0帧，说明暂时没有数据
    if (frames == 0) {
        return 0;
    }

    // 计算实际读取的字节数
    // 字节数 = 帧数 × 声道数 × 每样本字节数
    int bytesPerSample = snd_pcm_format_width(m_format) / 8;
    int actualSize = frames * m_channels * bytesPerSample;
    
    // 将数据从内部缓冲区复制到用户提供的缓冲区
    memcpy(buffer, m_pcmBuffer, actualSize);

    return actualSize;
}

/**
 * @brief 反初始化音频采集设备
 * @details 释放所有ALSA资源，防止内存泄漏
 *          知识点：ALSA资源释放顺序
 */
void ALSAAudioCapture::Uninitialize()
{
    // 先停止采集
    StopCapture();

    // 释放PCM数据缓冲区
    if (m_pcmBuffer) {
        free(m_pcmBuffer);
        m_pcmBuffer = nullptr;
        m_pcmBufferSize = 0;
    }

    // 知识点：关闭PCM设备
    // snd_pcm_close - 关闭PCM设备句柄，释放相关资源
    if (m_pcmHandle) {
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
    }

    m_bInitialized = false;
    printf("ALSAAudioCapture uninitialized\n");
}


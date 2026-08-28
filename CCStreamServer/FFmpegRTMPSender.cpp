/**
 * @file FFmpegRTMPSender.cpp
 * @brief FFmpeg RTMP推流器实现
 * @details 使用FFmpeg库实现RTMP协议推流，支持多路推流
 *          知识点：FFmpeg格式API、RTMP协议、音视频同步、时间戳处理
 */

#include "FFmpegRTMPSender.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/**
 * TCP推流帧率：15fps
RTMP推流帧率：15fps
两者使用同一编码器，帧率一致。虽然 FFmpegRTMPSender 构造函数默认值为25fps，
但实际初始化时会传入编码器的帧率（15fps），因此实际推流帧率为15fps。
 * @brief 构造函数
 * @details 初始化所有成员变量为默认值
 */
FFmpegRTMPSender::FFmpegRTMPSender()
    : m_bStreaming(false)        // 推流状态：未开始
    , m_bInitialized(false)      // 初始化标志：未初始化
    , m_width(0)                 // 视频宽度
    , m_height(0)                 // 视频高度
    , m_fps(15)                   // 帧率：15fps
    , m_bitrate(2000000)          // 码率：2Mbps
    , m_spsSize(0)                // SPS（Sequence Parameter Set）大小
    , m_ppsSize(0)                // PPS（Picture Parameter Set）大小
    , m_bSpsPpsSent(false)        // SPS/PPS是否已发送标志
    , m_videoPts(0)                // 视频时间戳
    , m_audioPts(0)                // 音频时间戳
    , m_startTime(0)              // 开始时间戳（毫秒）
{
    memset(m_sps, 0, sizeof(m_sps));  // 清零SPS缓冲区
    memset(m_pps, 0, sizeof(m_pps));  // 清零PPS缓冲区
}

FFmpegRTMPSender::~FFmpegRTMPSender()
{
    Close();
}

/**
 * @brief 初始化RTMP推流器
 * @details 保存推流参数（分辨率、帧率、码率），为每个RTMP URL创建独立的输出上下文（支持多路推流），
 *          初始化时间戳并设置推流状态。如果任何一路推流初始化失败，则关闭所有已创建的上下文并返回失败。
 * @param width 视频宽度
 * @param height 视频高度
 * @param fps 帧率
 * @param bitrate 码率（bps）
 * @param rtmpUrls RTMP服务器URL列表（支持多路推流）
 * @return true=成功，false=失败
 */
bool FFmpegRTMPSender::Initialize(int width, int height, int fps, int bitrate,
                                  const std::vector<std::string>& rtmpUrls)
{
    if (m_bInitialized) {
        printf("FFmpegRTMPSender already initialized\n");
        return false;
    }

    m_width = width;
    m_height = height;
    m_fps = fps;
    m_bitrate = bitrate;
    m_rtmpUrls = rtmpUrls;

    // 初始化所有输出上下文
    for (const auto& url : rtmpUrls) {
        if (!InitOutputContext(url)) {
            printf("Failed to initialize output context for: %s\n", url.c_str());
            Close();
            return false;
        }
    }

    m_bInitialized = true;
    m_bStreaming = true;
    m_startTime = av_gettime() / 1000; // 毫秒
    m_videoPts = 0;
    m_audioPts = 0;

    printf("FFmpegRTMPSender initialized: %dx%d@%dfps, %d bps, %zu outputs\n",
           width, height, fps, bitrate, rtmpUrls.size());

    return true;
}

/**
 * @brief 初始化输出上下文（为每个RTMP URL创建独立的输出上下文）
 * @details 知识点：FFmpeg输出上下文初始化
 *          1. 分配输出上下文（avformat_alloc_output_context2）
 *          2. 打开输出URL（avio_open）
 *          3. 创建视频流和音频流
 *          4. 设置流参数（编码器、分辨率、帧率等）
 *          5. 写入头部（avformat_write_header）
 * @param rtmpUrl RTMP服务器URL（如：rtmp://server/app/stream）
 * @return true=成功，false=失败
 */
bool FFmpegRTMPSender::InitOutputContext(const std::string& rtmpUrl)
{
    AVFormatContext* fmtCtx = nullptr;
    int ret;

    // 知识点：分配输出上下文
    // avformat_alloc_output_context2 - 分配并初始化输出格式上下文
    // 参数：
    //   - &fmtCtx: 输出参数，分配的上下文指针
    //   - nullptr: 格式名称（nullptr=自动检测，根据URL或扩展名）
    //   - "flv": 格式名称（FLV格式，RTMP使用）
    //   - rtmpUrl: 输出URL
    ret = avformat_alloc_output_context2(&fmtCtx, nullptr, "flv", rtmpUrl.c_str());
    if (ret < 0 || !fmtCtx) {
        printf("Could not create output context for %s\n", rtmpUrl.c_str());
        return false;
    }

    // 知识点：打开输出URL
    // avio_open - 打开输出IO上下文（建立网络连接）
    // AVIO_FLAG_WRITE: 写模式（推流）
    if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx->pb, rtmpUrl.c_str(), AVIO_FLAG_WRITE);


        if (ret < 0) {
            printf("Could not open output URL: %s, error: %d\n", rtmpUrl.c_str(), ret);
            avformat_free_context(fmtCtx);
            return false;
        }
    }

    // 知识点：创建视频流
    // avformat_new_stream - 在输出上下文中创建新的流
    // 参数2：编码器（nullptr=使用外部编码器，不在这里创建）
    AVStream* videoStream = avformat_new_stream(fmtCtx, nullptr);
    if (!videoStream) {
        printf("Could not create video stream\n");
        avio_closep(&fmtCtx->pb);
        avformat_free_context(fmtCtx);
        return false;
    }

    // 知识点：设置视频流参数
    videoStream->id = fmtCtx->nb_streams - 1;              // 流ID
    videoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO; // 媒体类型：视频
    videoStream->codecpar->codec_id = AV_CODEC_ID_H264;     // 编码器：H264
    videoStream->codecpar->width = m_width;                 // 视频宽度
    videoStream->codecpar->height = m_height;               // 视频高度
    videoStream->codecpar->format = AV_PIX_FMT_YUV420P;     // 像素格式：YUV420P
    videoStream->time_base = (AVRational){1, m_fps * 1000}; // 时间基：毫秒单位
    videoStream->avg_frame_rate = (AVRational){m_fps, 1};    // 平均帧率

    // 知识点：创建音频流
    // 如果推流包含音频，需要创建音频流
    AVStream* audioStream = avformat_new_stream(fmtCtx, nullptr);
    if (audioStream) {
        audioStream->id = fmtCtx->nb_streams - 1;
        audioStream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;  // 媒体类型：音频
        audioStream->codecpar->codec_id = AV_CODEC_ID_AAC;       // 编码器：AAC
        audioStream->codecpar->sample_rate = 44100;              // 采样率：44.1kHz
        audioStream->codecpar->channels = 2;                     // 声道数：立体声
        audioStream->codecpar->channel_layout = AV_CH_LAYOUT_STEREO; // 声道布局
        audioStream->codecpar->format = AV_SAMPLE_FMT_FLTP;      // 采样格式：浮点平面
        audioStream->time_base = (AVRational){1, 44100};        // 时间基：采样率
    }

    // 知识点：写入头部信息
    // avformat_write_header - 写入文件/流头部（FLV头部、RTMP握手等）
    // 必须在发送数据前调用
    ret = avformat_write_header(fmtCtx, nullptr);
    if (ret < 0) {
        printf("Error writing header to %s, error: %d\n", rtmpUrl.c_str(), ret);
        avio_closep(&fmtCtx->pb);
        avformat_free_context(fmtCtx);
        return false;
    }

    // 将输出上下文添加到列表（支持多路推流）
    m_outputContexts.push_back(fmtCtx);
    printf("Successfully initialized output context: %s\n", rtmpUrl.c_str());

    return true;
}

/**
 * @brief 发送H264视频帧
 * @details 知识点：H264 NALU解析、RTMP视频包发送
 *          1. 解析NALU，提取SPS/PPS（如果存在）
 *          2. 创建AVPacket
 *          3. 设置时间戳（PTS）
 *          4. 发送到所有输出上下文（多路推流）
 * @param data H264编码数据（包含NALU起始码）
 * @param size 数据大小（字节）
 * @param isKeyFrame 是否为关键帧（IDR帧）
 * @param pts 显示时间戳（毫秒）
 * @return true=成功，false=失败
 */
bool FFmpegRTMPSender::SendH264Frame(uint8_t* data, int size, bool isKeyFrame, int64_t pts)
{
    if (!m_bStreaming || !m_bInitialized) {
        return false;
    }

    // 知识点：解析H264 NALU，提取SPS/PPS
    // SPS（Sequence Parameter Set）：序列参数集，包含视频基本参数
    // PPS（Picture Parameter Set）：图像参数集，包含编码参数
    // NALU类型：低5位（data[4] & 0x1F）
    //   7 = SPS
    //   8 = PPS
    //   5 = IDR帧
    if (size > 4) {
        uint8_t nalType = data[4] & 0x1F;
        if (nalType == 7) { // SPS
            // 提取SPS（跳过0x00 0x00 0x00 0x01）
            int start = 0;//0不一定是0001，还要慢慢找
            while (start < size - 4 && 
                   !(data[start] == 0 && data[start+1] == 0 && 
                     data[start+2] == 0 && data[start+3] == 1)) 
            {
                start++;
            }
            if (start < size - 4) 
            {
                start += 4;
                int end = start;//找下一个0001的位置
                while (end < size - 4 && 
                       !(data[end] == 0 && data[end+1] == 0 && 
                         data[end+2] == 0 && data[end+3] == 1)) 
                {
                    end++;
                }
                if (end > start && end - start < 128) {
                    m_spsSize = end - start;
                    memcpy(m_sps, data + start, m_spsSize);
                }
            }


        } 
        else if (nalType == 8) { // PPS
            int start = 0;
            while (start < size - 4 && 
                   !(data[start] == 0 && data[start+1] == 0 && 
                     data[start+2] == 0 && data[start+3] == 1)) {
                start++;
            }
            if (start < size - 4) {
                start += 4;
                int end = start;
                while (end < size - 4 && 
                       !(data[end] == 0 && data[end+1] == 0 && 
                         data[end+2] == 0 && data[end+3] == 1)) {
                    end++;
                }
                if (end > start && end - start < 128) {
                    m_ppsSize = end - start;
                    memcpy(m_pps, data + start, m_ppsSize);
                }
            }
        }
    }

    // 发送到所有输出上下文
    bool allSuccess = true;
    for (auto* fmtCtx : m_outputContexts) {// 多个输出上下文（多平台推流）
        if (!WriteVideoFrame(fmtCtx, data, size, isKeyFrame, pts)) {
            allSuccess = false;
        }
    }

    return allSuccess;
}

/**
 * @brief 写入视频帧到输出上下文
 * @details 知识点：FFmpeg数据包写入、时间戳转换
 *          1. 创建AVPacket
 *          2. 复制H264数据
 *          3. 转换时间戳（从毫秒转为流时间基）
 *          4. 写入数据包（av_interleaved_write_frame）
 * @param fmtCtx 输出格式上下文
 * @param data H264数据
 * @param size 数据大小
 * @param isKeyFrame 是否为关键帧
 * @param pts 显示时间戳（毫秒）
 * @return true=成功，false=失败
 */
bool FFmpegRTMPSender::WriteVideoFrame(AVFormatContext* fmtCtx, uint8_t* data, int size,
                                      bool isKeyFrame, int64_t pts)
{
    if (!fmtCtx || !data || size <= 0) {
        return false;
    }

    AVStream* videoStream = fmtCtx->streams[0];
    if (!videoStream) {
        return false;
    }

    // 知识点：创建AVPacket
    // AVPacket：FFmpeg的数据包结构，用于存储编码后的音视频数据
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return false;
    }

    // 知识点：分配数据缓冲区
    // av_new_packet - 为数据包分配数据缓冲区
    int ret = av_new_packet(pkt, size);
    if (ret < 0) {
        av_packet_free(&pkt);
        return false;
    }

    // 复制H264数据到数据包
    memcpy(pkt->data, data, size);
    pkt->size = size;
    pkt->stream_index = videoStream->index;              // 流索引
    pkt->flags = isKeyFrame ? AV_PKT_FLAG_KEY : 0;       // 关键帧标志

    // 知识点：时间戳转换
    // av_rescale_q - 将时间戳从一个时间基转换到另一个时间基
    // 输入：pts（毫秒，时间基1/1000）
    // 输出：流时间基（videoStream->time_base）
    pkt->pts = av_rescale_q(pts, (AVRational){1, 1000}, videoStream->time_base);
    pkt->dts = pkt->pts;  
    pkt->duration = av_rescale_q(1000 / m_fps, (AVRational){1, 1000}, videoStream->time_base); // 帧持续时间

    // 知识点：写入数据包
    // av_interleaved_write_frame - 交错写入数据包（自动处理音视频交错）
    // 确保音视频数据按时间顺序交错发送
    ret = av_interleaved_write_frame(fmtCtx, pkt);
    
    av_packet_free(&pkt);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Error writing video frame: %s\n", errbuf);
        return false;
    }

    return true;
}

/**
 * @brief 发送AAC音频帧到所有RTMP输出流
 * @details 遍历所有输出上下文，对于包含音频流的上下文（nb_streams > 1），调用WriteAudioFrame写入音频数据。
 *          如果任何一路推流写入失败，返回false，但会继续尝试其他流。
 * @param data AAC编码数据
 * @param size 数据大小（字节）
 * @param pts 显示时间戳（毫秒）
 * @return true=所有流都成功，false=至少一路失败
 */
bool FFmpegRTMPSender::SendAACFrame(uint8_t* data, int size, int64_t pts)
{
    if (!m_bStreaming || !m_bInitialized) {
        return false;
    }

    bool allSuccess = true;
    for (auto* fmtCtx : m_outputContexts) {
        if (fmtCtx->nb_streams > 1) {
            if (!WriteAudioFrame(fmtCtx, data, size, pts)) {
                allSuccess = false;
            }
        }
    }

    return allSuccess;
}

/**
 * @brief 写入音频帧到输出上下文
 * @details 创建AVPacket并复制AAC数据，将时间戳从毫秒转换为音频流时间基，计算帧持续时间后写入数据包。
 *          使用av_interleaved_write_frame确保音视频数据按时间顺序交错发送。
 * @param fmtCtx 输出格式上下文
 * @param data AAC数据
 * @param size 数据大小
 * @param pts 显示时间戳（毫秒）
 * @return true=成功，false=失败
 */
bool FFmpegRTMPSender::WriteAudioFrame(AVFormatContext* fmtCtx, uint8_t* data, int size, int64_t pts)
{
    if (!fmtCtx || !data || size <= 0) {
        return false;
    }

    if (fmtCtx->nb_streams < 2) {
        return false; // 没有音频流
    }

    AVStream* audioStream = fmtCtx->streams[1];
    if (!audioStream) {
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return false;
    }

    int ret = av_new_packet(pkt, size);
    if (ret < 0) {
        av_packet_free(&pkt);
        return false;
    }

    memcpy(pkt->data, data, size);
    pkt->size = size;
    pkt->stream_index = audioStream->index;

    // 转换时间戳
    pkt->pts = av_rescale_q(pts, (AVRational){1, 1000}, audioStream->time_base);
    pkt->dts = pkt->pts;
    pkt->duration = av_rescale_q(size * 8 * 1000 / (44100 * 16 * 2), 
                                 (AVRational){1, 1000}, audioStream->time_base);

    ret = av_interleaved_write_frame(fmtCtx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        return false;
    }

    return true;
}

/**
 * @brief 关闭RTMP推流器并释放资源
 * @details 停止推流状态，遍历所有输出上下文，写入尾部信息（av_write_trailer），关闭IO连接并释放上下文资源，
 *          最后清空输出上下文列表并重置初始化标志。
 */
void FFmpegRTMPSender::Close()
{
    if (!m_bInitialized) {
        return;
    }

    m_bStreaming = false;

    // 关闭所有输出上下文
    for (auto* fmtCtx : m_outputContexts) {
        if (fmtCtx) {
            av_write_trailer(fmtCtx);
            if (fmtCtx->pb && !(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&fmtCtx->pb);
            }
            avformat_free_context(fmtCtx);
        }
    }

    m_outputContexts.clear();
    m_bInitialized = false;

    printf("FFmpegRTMPSender closed\n");
}


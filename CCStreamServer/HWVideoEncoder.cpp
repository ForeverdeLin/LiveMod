/**
 * @file HWVideoEncoder.cpp
 * @brief 硬件视频编码器实现
 * @details 使用FFmpeg硬件加速API实现硬件视频编码，支持硬件和软件编码自动切换
 *          支持：VAAPI（Intel/AMD）、NVENC（NVIDIA）、V4L2M2M（Linux）、QSV（Intel）
 *          实现方式：硬件编码时，将YUV420P软件帧上传到GPU硬件帧，然后发送硬件帧给编码器；
 *          软件编码时，直接发送软件帧给编码器。使用av_hwframe_transfer_data实现CPU到GPU的数据传输。
 *          知识点：FFmpeg硬件加速API、硬件编码器初始化、软件帧到硬件帧的格式转换
 */

#include "HWVideoEncoder.h"
#include <stdio.h>
#include <string.h>
#include <vector>

/**
 * @brief 构造函数
 * @details 初始化硬件编码器对象
 */
HWVideoEncoder::HWVideoEncoder()
    : m_bInitialized(false)          // 初始化标志：未初始化
    , m_currentHWType(HWAccelType::NONE) // 当前硬件加速类型：无
    , m_codecCtx(nullptr)            // FFmpeg编码器上下文
    , m_hwDeviceCtx(nullptr)         // 硬件设备上下文
    , m_hwFrame(nullptr)             // 硬件帧（GPU内存，硬件编码时使用）
    , m_swFrame(nullptr)             // 软件帧（CPU内存，存储YUV420P输入数据）
    , m_bsfCtx(nullptr)               // Bitstream filter上下文
    , m_frameCount(0)                // 编码帧计数
{
    memset(&m_params, 0, sizeof(m_params));
}

HWVideoEncoder::~HWVideoEncoder()
{
    Uninitialize();
}

/**
 * @brief 检测可用的硬件加速类型
 * @details 实现方式：遍历VAAPI、NVENC、V4L2M2M、QSV等硬件加速类型，通过av_hwdevice_ctx_create
 *          尝试创建对应的硬件设备上下文。若创建成功（返回0），说明该硬件加速可用，将其加入列表
 *          并立即释放设备上下文；若所有硬件加速都不可用，则返回NONE类型。
 *          
 *          知识点：FFmpeg硬件设备检测、AVBufferRef引用计数管理
 *          支持的硬件加速类型：
 *          - VAAPI: Intel/AMD GPU（Linux）
 *          - NVENC: NVIDIA GPU
 *          - V4L2M2M: Linux Video4Linux2（树莓派等）
 *          - QSV: Intel Quick Sync Video
 * @return 可用的硬件加速类型列表
 */
std::vector<HWAccelType> HWVideoEncoder::DetectAvailableHWAccel()
{
    std::vector<HWAccelType> available;

    // 知识点：检测VAAPI（Intel/AMD GPU）
    // av_hwdevice_ctx_create - 创建硬件设备上下文（这里只是测试有没有，不是真的要创建）
    // AV_HWDEVICE_TYPE_VAAPI: VAAPI设备类型
    // 返回：0=成功，<0=失败（设备不存在或不支持）
    // 知识点：AVBufferRef引用计数管理
    // av_hwdevice_ctx_create成功时创建新的AVBufferRef，引用计数为1
    // av_buffer_unref会减少引用计数，当为0时自动释放内存并将指针设为nullptr
    // 失败时，函数通常会将hwDeviceCtx设为nullptr，但为了安全起见，无论成功失败都应该检查并释放
    AVBufferRef* hwDeviceCtx = nullptr;
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
    if (ret == 0 && hwDeviceCtx) {
        available.push_back(HWAccelType::VAAPI);
    }
    // 无论成功失败都释放引用（失败时hwDeviceCtx通常为nullptr，av_buffer_unref会安全处理）
    if (hwDeviceCtx) {
        av_buffer_unref(&hwDeviceCtx);
    }

    // 知识点：检测NVENC（NVIDIA GPU）
    // AV_HWDEVICE_TYPE_CUDA: CUDA设备类型（NVENC使用CUDA）
    ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (ret == 0 && hwDeviceCtx) {
        available.push_back(HWAccelType::NVENC);
    }
    if (hwDeviceCtx) {
        av_buffer_unref(&hwDeviceCtx);
    }

    // 知识点：检测V4L2M2M（Linux Video4Linux2）
    // 常用于树莓派等嵌入式设备
    ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_V4L2M2M, nullptr, nullptr, 0);
    if (ret == 0 && hwDeviceCtx) {
        available.push_back(HWAccelType::V4L2M2M);
    }
    if (hwDeviceCtx) {
        av_buffer_unref(&hwDeviceCtx);
    }

    // 知识点：检测QSV（Intel Quick Sync Video）
    // Intel集成显卡的硬件编码
    ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_QSV, nullptr, nullptr, 0);
    if (ret == 0 && hwDeviceCtx) {
        available.push_back(HWAccelType::QSV);
    }
    if (hwDeviceCtx) {
        av_buffer_unref(&hwDeviceCtx);
    }

    // 如果没有硬件加速，使用软件编码
    if (available.empty()) {
        available.push_back(HWAccelType::NONE);
    }

    return available;
}

/**
 * @brief 初始化编码器
 * @details 实现方式：先检查是否已初始化避免重复初始化；保存编码参数和硬件类型；若指定了硬件加速，
 *          则尝试初始化硬件设备，失败时自动回退到软件编码；然后创建编码器上下文（硬件或软件）；
 *          最后设置初始化标志和帧计数。整个过程采用"硬件优先，失败回退"的策略，确保编码器总能成功初始化。
 * @param params 编码器参数
 * @return 成功返回true
 */
bool HWVideoEncoder::Initialize(const EncoderParams& params)//其他函数服务这个初始化函数的
{
    if (m_bInitialized) {
        printf("HWVideoEncoder already initialized\n");
        return false;
    }

    m_params = params;
    m_currentHWType = params.hwType;//设置了硬件编码器类型

    // 如果指定了硬件加速， 尝试初始化硬件设备上下文
    if (m_currentHWType != HWAccelType::NONE) {
        if (!InitHWDevice(m_currentHWType)) {
            printf("Failed to initialize hardware device, falling back to software encoding\n");
            m_currentHWType = HWAccelType::NONE;
        }
    }

    // 创建编码器上下文
    if (!CreateEncoderContext(params)) {
        printf("Failed to create encoder context\n");
        Uninitialize();
        return false;
    }

    m_bInitialized = true;
    m_frameCount = 0;

    printf("HWVideoEncoder initialized: %dx%d@%dfps, %d bps, HW: %d\n",
           params.width, params.height, params.fps, params.bitrate, 
           (int)m_currentHWType);

    return true;
}

/**
 * @brief 初始化硬件设备上下文（在里面创建硬件设备上下文）   
 * @details 实现方式：通过switch语句将自定义的HWAccelType枚举映射到FFmpeg的AVHWDeviceType
 *          （注意NVENC映射到AV_HWDEVICE_TYPE_CUDA，因为NVENC基于CUDA设备）；然后调用
 *          av_hwdevice_ctx_create创建硬件设备上下文并存储在m_hwDeviceCtx中；若创建失败，
 *          使用av_strerror获取错误信息并打印后返回false。
 * @param hwType 硬件加速类型
 * @return 成功返回true，失败返回false
 */
bool HWVideoEncoder::InitHWDevice(HWAccelType hwType)
{
    AVHWDeviceType deviceType = AV_HWDEVICE_TYPE_NONE;//先设为软件

    switch (hwType) {
        case HWAccelType::VAAPI:
            deviceType = AV_HWDEVICE_TYPE_VAAPI;
            break;
        case HWAccelType::NVENC:
            deviceType = AV_HWDEVICE_TYPE_CUDA;
            break;
        case HWAccelType::V4L2M2M:
            deviceType = AV_HWDEVICE_TYPE_V4L2M2M;
            break;
        case HWAccelType::QSV:
            deviceType = AV_HWDEVICE_TYPE_QSV;
            break;
        default:
            return false;
    }
    //2.创建硬件设备上下文 → av_hwdevice_ctx_create（连接GPU设备）
    int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, deviceType, nullptr, nullptr, 0);///这里才是真创
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Failed to create hardware device context: %s\n", errbuf);
        return false;
    }

    return true;
}

/**
 * @brief 创建编码器上下文
 * @details 实现方式：首先根据硬件类型查找对应的硬件编码器（h264_nvenc/h264_vaapi/h264_v4l2m2m），
 *          若找不到则回退到软件编码器（x264）；然后创建编码器上下文并设置编码参数（分辨率、帧率、
 *          码率、GOP等）；若使用硬件编码，设置硬件像素格式（CUDA/VAAPI/QSV等）并创建硬件帧上下文，
 *          通过av_buffer_ref将硬件设备上下文关联到编码器上下文；接着通过av_opt_set设置编码预设
 *          和profile；最后调用avcodec_open2打开编码器，并分配用于存储输入YUV数据的软件帧。
 * @param params 编码器参数
 * @return 成功返回true，失败返回false
 */
bool HWVideoEncoder::CreateEncoderContext(const EncoderParams& params)
{
    // 查找编码器
    const AVCodec* codec = nullptr;
    if (m_currentHWType != HWAccelType::NONE) {
        // 尝试硬件编码器
        codec = avcodec_find_encoder_by_name("h264_nvenc"); // NVENC
        if (!codec && m_currentHWType == HWAccelType::VAAPI) {
            codec = avcodec_find_encoder_by_name("h264_vaapi"); // VAAPI
        }
        if (!codec && m_currentHWType == HWAccelType::V4L2M2M) {
            codec = avcodec_find_encoder_by_name("h264_v4l2m2m"); // V4L2M2M
        }
    }

    // 如果硬件编码器不可用，使用软件编码器
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        m_currentHWType = HWAccelType::NONE;
    }

    if (!codec) {
        printf("H.264 encoder not found\n");
        return false;
    }

    // 创建编码器上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        printf("Could not allocate video codec context\n");
        return false;
    }

    // 设置编码参数
    m_codecCtx->width = params.width;
    m_codecCtx->height = params.height;
    m_codecCtx->time_base = (AVRational){1, params.fps};
    m_codecCtx->framerate = (AVRational){params.fps, 1};
    m_codecCtx->bit_rate = params.bitrate;
    m_codecCtx->gop_size = params.gopSize;
    m_codecCtx->max_b_frames = 0; // 实时编码不使用B帧

    // 根据编码器类型设置像素格式
    enum AVPixelFormat hwFormat = AV_PIX_FMT_NONE;
    if (m_currentHWType != HWAccelType::NONE && m_hwDeviceCtx) {
        // 硬件编码：根据硬件类型设置对应的硬件像素格式
        switch (m_currentHWType) {
            case HWAccelType::NVENC:
                hwFormat = AV_PIX_FMT_CUDA;
                break;
            case HWAccelType::VAAPI:
                hwFormat = AV_PIX_FMT_VAAPI;
                break;
            case HWAccelType::QSV:
                hwFormat = AV_PIX_FMT_QSV;
                break;
            case HWAccelType::V4L2M2M:
                // V4L2M2M通常使用YUV420P
                hwFormat = AV_PIX_FMT_YUV420P;
                break;
            default:
                hwFormat = AV_PIX_FMT_YUV420P;
                break;
        }
        m_codecCtx->pix_fmt = hwFormat;
        m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);//将硬件设备上下文关联到编码器上下文

        // 创建硬件帧上下文（HWFramesContext）
        // 硬件编码器需要硬件帧上下文来管理GPU内存中的帧
        AVBufferRef* hwFramesCtx = av_hwframe_ctx_alloc(m_hwDeviceCtx);
        if (!hwFramesCtx) {
            printf("Failed to allocate hardware frames context\n");
            avcodec_free_context(&m_codecCtx);
            return false;
        }

        AVHWFramesContext* framesCtx = (AVHWFramesContext*)hwFramesCtx->data;
        framesCtx->format = hwFormat;//输出格式
        framesCtx->sw_format = AV_PIX_FMT_YUV420P;  // 软件格式（输入格式）
        framesCtx->width = params.width;
        framesCtx->height = params.height;

        int ret = av_hwframe_ctx_init(hwFramesCtx);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("Failed to initialize hardware frames context: %s\n", errbuf);
            av_buffer_unref(&hwFramesCtx);
            avcodec_free_context(&m_codecCtx);
            return false;
        }

        m_codecCtx->hw_frames_ctx = hwFramesCtx;
    } else {
        // 软件编码：使用YUV420P格式
        m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    // 设置编码预设和profile
    if (!params.preset.empty()) {
        av_opt_set(m_codecCtx->priv_data, "preset", params.preset.c_str(), 0);
    }
    if (!params.profile.empty()) {
        av_opt_set(m_codecCtx->priv_data, "profile", params.profile.c_str(), 0);
    }
    
    // 知识点：设置H.264输出格式为Annex-B（适合流媒体）
    // Annex-B格式：使用起始码（0x00000001）分隔NALU，适合RTMP/RTSP等流媒体协议
    // AVCC格式：使用长度前缀分隔NALU，适合MP4/MOV等容器格式
    // 对于libx264编码器，通过x264-params设置annexb=1
    // 对于硬件编码器，可能需要通过其他方式设置
    if (m_currentHWType == HWAccelType::NONE) {
        // 软件编码器（libx264）：设置Annex-B格式
        av_opt_set(m_codecCtx->priv_data, "x264-params", "annexb=1", 0);
    }
    // 注意：硬件编码器（NVENC/VAAPI）默认可能输出AVCC格式
    // 如果后续处理需要Annex-B格式，可能需要进行格式转换

    // 打开编码器
    int ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Could not open codec: %s\n", errbuf);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    // 分配软件帧：用于存储输入YUV数据（CPU内存）
    // 作用：1) 将外部连续YUV数据转换为FFmpeg的AVFrame格式（分离的Y/U/V平面）
    //      2) 软件编码时，直接使用软件帧（YUV420P格式）发送给编码器
    //      3) 硬件编码时，软件帧作为中间缓冲区，通过UploadFrameToHW上传到GPU硬件帧
    m_swFrame = av_frame_alloc();
    if (!m_swFrame) {
        printf("Could not allocate video frame\n");
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_swFrame->format = AV_PIX_FMT_YUV420P;
    m_swFrame->width = params.width;
    m_swFrame->height = params.height;
    ret = av_frame_get_buffer(m_swFrame, 32);
    if (ret < 0) {
        printf("Could not allocate frame buffer\n");
        av_frame_free(&m_swFrame);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    // 知识点：为硬件编码器初始化Bitstream Filter（AVCC转Annex-B）
    // 硬件编码器（NVENC/VAAPI等）默认输出AVCC格式（使用长度前缀）
    // 但流媒体（RTMP/RTSP）需要Annex-B格式（使用起始码）
    // h264_mp4toannexb filter可以将AVCC格式转换为Annex-B格式
    if (m_currentHWType != HWAccelType::NONE) {
        const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
        if (!bsf) {
            printf("Warning: h264_mp4toannexb filter not found, hardware encoder may output AVCC format\n");
        } else {
            ret = av_bsf_alloc(bsf, &m_bsfCtx);
            if (ret < 0) {
                printf("Warning: Failed to allocate bitstream filter context\n");
            } else {
                // 设置编码器参数到filter
                ret = avcodec_parameters_from_context(m_bsfCtx->par_in, m_codecCtx);
                if (ret < 0) {
                    printf("Warning: Failed to set codec parameters for bitstream filter\n");
                    av_bsf_free(&m_bsfCtx);
                    m_bsfCtx = nullptr;
                } else {
                    ret = av_bsf_init(m_bsfCtx);
                    if (ret < 0) {
                        char errbuf[256];
                        av_strerror(ret, errbuf, sizeof(errbuf));
                        printf("Warning: Failed to initialize bitstream filter: %s\n", errbuf);
                        av_bsf_free(&m_bsfCtx);
                        m_bsfCtx = nullptr;
                    } else {
                        printf("Bitstream filter initialized for Annex-B conversion\n");
                    }
                }
            }
        }
    }

    return true;
}

/**
 * @brief 编码一帧YUV数据
 * @details 实现方式：首先进行参数校验和GOP控制（判断是否需要关键帧）；然后将外部连续YUV数据
 *          复制到软件帧的Y、U、V平面，并设置PTS时间戳；若使用硬件编码，通过UploadFrameToHW
 *          将软件帧上传到GPU硬件帧，然后发送硬件帧给编码器；若使用软件编码，直接发送软件帧；
 *          接着通过avcodec_receive_packet接收编码后的H.264数据包；最后将编码数据复制到输出
 *          缓冲区，并判断是否为关键帧。整个过程采用"发送-接收"模式，正确区分硬件和软件编码流程。
 * 核心流程：填充软件帧 → (硬件编码：上传到硬件帧) → 发送到编码器 → 接收编码后数据 → 输出。
 * @param yuvData YUV420P数据
 * @param yuvSize 数据大小
 * @param output 输出H.264数据缓冲区
 * @param outputSize 输出缓冲区大小（输入时表示缓冲区容量，输出时表示实际数据大小）
 * @param isKeyFrame 输出参数，是否为关键帧
 * @return 成功返回true，失败返回false
 */
bool HWVideoEncoder::EncodeFrame(uint8_t* yuvData, int yuvSize, uint8_t* output,
                                 int* outputSize, bool* isKeyFrame)
{
    if (!m_bInitialized || !yuvData || !output || !outputSize || !isKeyFrame) {
        return false;
    }

    // 判断是否需要关键帧（GOP控制）
    bool forceKeyFrame = (m_frameCount % m_params.gopSize == 0);
    m_frameCount++;

    // 填充软件帧数据（YUV420P格式）
    int ySize = m_params.width * m_params.height;
    int uvSize = ySize / 4;

    if (yuvSize < ySize + uvSize * 2) {
        printf("YUV data size too small\n");
        return false;
    }

    // 复制Y、U、V数据到软件帧
    memcpy(m_swFrame->data[0], yuvData, ySize);
    memcpy(m_swFrame->data[1], yuvData + ySize, uvSize);
    memcpy(m_swFrame->data[2], yuvData + ySize + uvSize, uvSize);
    m_swFrame->pts = m_frameCount - 1;  // 设置时间戳
    
    // 如果需要强制关键帧，设置帧类型为I帧
    // 知识点：AVFrame的pict_type字段用于指定帧类型,AVFrame是ffmepg自己的
    // AV_PICTURE_TYPE_I: I帧（关键帧），独立编码，不依赖其他帧
    // AV_PICTURE_TYPE_P: P帧（预测帧），依赖前一帧
    // AV_PICTURE_TYPE_NONE: 让编码器自动决定
    if (forceKeyFrame) {
        m_swFrame->pict_type = AV_PICTURE_TYPE_I;
    } else {
        m_swFrame->pict_type = AV_PICTURE_TYPE_NONE;  // 让编码器自动决定
    }

    AVFrame* encodeFrame = nullptr;

    // 根据编码类型选择帧：硬件编码需要硬件帧，软件编码使用软件帧
    if (m_currentHWType != HWAccelType::NONE) 
    {
        // 硬件编码：将软件帧上传到GPU硬件帧
        if (!m_hwFrame) {      //创建硬件帧
            m_hwFrame = av_frame_alloc();
            if (!m_hwFrame) {
                printf("Failed to allocate hardware frame\n");
                return false;
            }
        }

        // 上传软件帧到硬件帧（GPU内存）
        if (!UploadFrameToHW(m_hwFrame)) {
            printf("Failed to upload frame to hardware\n");
            return false;
        }

        encodeFrame = m_hwFrame;  // 使用硬件帧
    } 
    else 
    {
        // 软件编码：直接使用软件帧
        encodeFrame = m_swFrame;
    }

    // 发送帧到编码器
    int ret = avcodec_send_frame(m_codecCtx, encodeFrame);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Error sending frame to encoder: %s\n", errbuf);
        return false;
    }

    // 接收编码后的数据
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return false;
    }

    ret = avcodec_receive_packet(m_codecCtx, pkt);
    if (ret < 0) {
        av_packet_free(&pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return false; // 需要更多输入或已结束
        }
        return false;
    }

    // 知识点：硬件编码器输出AVCC格式，需要转换为Annex-B格式
    // 使用bitstream filter将AVCC（长度前缀）转换为Annex-B（起始码）
    AVPacket* filteredPkt = pkt;
    if (m_currentHWType != HWAccelType::NONE && m_bsfCtx) {
        // 发送数据包到filter
        ret = av_bsf_send_packet(m_bsfCtx, pkt);
        if (ret < 0) {
            printf("Warning: Failed to send packet to bitstream filter\n");
            av_packet_free(&pkt);
            return false;
        }

        // 创建新的数据包用于接收转换后的数据
        AVPacket* filtered = av_packet_alloc();
        if (!filtered) {
            av_packet_free(&pkt);
            return false;
        }

        // 从filter接收转换后的数据包
        ret = av_bsf_receive_packet(m_bsfCtx, filtered);
        if (ret < 0) {
            av_packet_free(&filtered);
            av_packet_free(&pkt);
            return false;
        }

        // 使用转换后的数据包
        av_packet_free(&pkt);
        filteredPkt = filtered;
    }

    // 复制数据到输出缓冲区
    if (filteredPkt->size > *outputSize) {
        printf("Output buffer too small: need %d, got %d\n", filteredPkt->size, *outputSize);
        av_packet_free(&filteredPkt);
        return false;
    }

    memcpy(output, filteredPkt->data, filteredPkt->size);
    *outputSize = filteredPkt->size;
    *isKeyFrame = (filteredPkt->flags & AV_PKT_FLAG_KEY) != 0;

    av_packet_free(&filteredPkt);
    return true;
}

/**
 * @brief 将软件帧上传到硬件帧（GPU内存）
 * @details 实现方式：使用av_hwframe_transfer_data将已填充的软件帧（m_swFrame）数据从CPU内存
 *          上传到GPU内存的硬件帧。需要确保硬件帧已分配且格式正确。
 *          注意：此函数假设m_swFrame已经由调用者填充好数据，只需要进行硬件帧分配和数据传输。
 * @param hwFrame 输出的硬件帧（GPU内存）
 * @return 成功返回true，失败返回false
 */
bool HWVideoEncoder::UploadFrameToHW(AVFrame* hwFrame)
{
    if (!m_bInitialized || !hwFrame || !m_hwDeviceCtx) {
        return false;
    }

    // 如果使用软件编码，不需要上传
    if (m_currentHWType == HWAccelType::NONE) {
        return false;
    }

/*    // 1. 确保软件帧已分配并已填充数据     保险机制
    if (!m_swFrame) {
        printf("Software frame not allocated\n");
        return false;
    }

    // 2. 获取硬件帧格式（从编码器上下文）
    // 硬件编码器通常需要特定的硬件格式，如AV_PIX_FMT_CUDA、AV_PIX_FMT_VAAPI等
    enum AVPixelFormat hwFormat = AV_PIX_FMT_NONE;
    if (m_codecCtx && m_codecCtx->hw_frames_ctx) {
        AVHWFramesContext* framesCtx = (AVHWFramesContext*)m_codecCtx->hw_frames_ctx->data;
        hwFormat = framesCtx->format;
    } else {
        // 如果没有硬件帧上下文，根据硬件类型推断格式
        switch (m_currentHWType) {
            case HWAccelType::NVENC:
                hwFormat = AV_PIX_FMT_CUDA;
                break;
            case HWAccelType::VAAPI:
                hwFormat = AV_PIX_FMT_VAAPI;
                break;
            case HWAccelType::QSV:
                hwFormat = AV_PIX_FMT_QSV;
                break;
            default:
                printf("Unknown hardware type for format\n");
                return false;
        }
    }

    // 3. 确保硬件帧已分配且格式正确
    // 如果硬件帧未初始化或格式不匹配，需要重新分配
    if (!hwFrame->data[0] || hwFrame->format != hwFormat) {
        // 释放旧的硬件帧（如果已分配）
        av_frame_unref(hwFrame);

        // 创建硬件帧上下文（如果还没有）
        if (!m_codecCtx->hw_frames_ctx) {
            AVBufferRef* hwFramesCtx = av_hwframe_ctx_alloc(m_hwDeviceCtx);
            if (!hwFramesCtx) {
                printf("Failed to allocate hardware frames context\n");
                return false;
            }

            AVHWFramesContext* framesCtx = (AVHWFramesContext*)hwFramesCtx->data;
            framesCtx->format = hwFormat;
            framesCtx->sw_format = AV_PIX_FMT_YUV420P;
            framesCtx->width = m_params.width;
            framesCtx->height = m_params.height;

            int ret = av_hwframe_ctx_init(hwFramesCtx);
            if (ret < 0) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                printf("Failed to initialize hardware frames context: %s\n", errbuf);
                av_buffer_unref(&hwFramesCtx);
                return false;
            }

            m_codecCtx->hw_frames_ctx = hwFramesCtx;
        }

        // 分配硬件帧
        int ret = av_hwframe_get_buffer(m_codecCtx->hw_frames_ctx, hwFrame, 0);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("Failed to allocate hardware frame: %s\n", errbuf);
            return false;
        }
    }
*/
    // 4. 将软件帧数据上传到硬件帧
    int ret = av_hwframe_transfer_data(hwFrame, m_swFrame, 0);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Failed to transfer frame to hardware: %s\n", errbuf);
        return false;
    }

    // 5. 复制元数据（PTS、宽度、高度、帧类型等）
    hwFrame->pts = m_swFrame->pts;
    hwFrame->width = m_swFrame->width;
    hwFrame->height = m_swFrame->height;
    hwFrame->pict_type = m_swFrame->pict_type;  // 复制帧类型（I帧/P帧）

    return true;
}

void HWVideoEncoder::Uninitialize()
{
    if (!m_bInitialized) {
        return;
    }

    if (m_bsfCtx) {
        av_bsf_free(&m_bsfCtx);
        m_bsfCtx = nullptr;
    }

    if (m_hwFrame) {
        av_frame_free(&m_hwFrame);
        m_hwFrame = nullptr;
    }

    if (m_swFrame) {
        av_frame_free(&m_swFrame);
        m_swFrame = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }

    m_bInitialized = false;
    m_frameCount = 0;

    printf("HWVideoEncoder uninitialized\n");
}


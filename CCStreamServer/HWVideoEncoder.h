#ifndef HWVIDEOENCODER_H
#define HWVIDEOENCODER_H

#include <stdint.h>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavcodec/bsf.h>
}

/**
 * @brief 硬件加速编码器类型
 */
enum class HWAccelType {
    NONE,           // 软件编码（x264）
    VAAPI,          // Intel/AMD VAAPI
    NVENC,          // NVIDIA NVENC
    V4L2M2M,        // Video4Linux2 Memory-to-Memory
    QSV             // Intel Quick Sync Video
};

/**
 * @brief 编码器参数
 */
struct EncoderParams {
    int width;                      // 视频宽度
    int height;                     // 视频高度
    int fps;                        // 帧率
    int bitrate;                    // 码率（bps）
    int gopSize;                    // GOP大小（关键帧间隔）
    HWAccelType hwType;              // 硬件加速类型
    std::string preset;             // 编码预设（如"fast", "medium", "slow"）
    std::string profile;            // 编码profile（如"baseline", "main", "high"）
};

/**
 * @brief 硬件加速视频编码器
 * 支持VAAPI、NVENC等硬件编码，自动fallback到软件编码
 */
class HWVideoEncoder
{
public:
    HWVideoEncoder();
    ~HWVideoEncoder();

    /**
     * @brief 初始化编码器
     * @param params 编码器参数
     * @return 成功返回true
     */
    bool Initialize(const EncoderParams& params);

    /**
     * @brief 编码一帧YUV数据
     * @param yuvData YUV420P数据
     * @param yuvSize 数据大小
     * @param output 输出H.264数据缓冲区
     * @param outputSize 输出缓冲区大小（输入时表示缓冲区容量，输出时表示实际数据大小）
     * @param isKeyFrame 输出参数，是否为关键帧
     * @return 成功返回true
     */
    bool EncodeFrame(uint8_t* yuvData, int yuvSize, uint8_t* output, 
                     int* outputSize, bool* isKeyFrame);

    /**
     * @brief 释放编码器
     */
    void Uninitialize();

    /**
     * @brief 检测可用的硬件加速类型
     * @return 可用的硬件加速类型列表
     */
    static std::vector<HWAccelType> DetectAvailableHWAccel();

    /**
     * @brief 获取当前使用的编码器类型
     */
    HWAccelType GetEncoderType() const { return m_currentHWType; }

private:
    /**
     * @brief 初始化硬件设备上下文
     */
    bool InitHWDevice(HWAccelType hwType);

    /**
     * @brief 创建编码器上下文
     */
    bool CreateEncoderContext(const EncoderParams& params);

    /**
     * @brief 将软件帧上传到硬件帧（GPU内存）
     * @details 将已填充的m_swFrame上传到硬件帧，调用前需确保m_swFrame已填充数据
     */
    bool UploadFrameToHW(AVFrame* hwFrame);

    /**
     * @brief 从硬件表面下载编码后的数据
     */
    bool DownloadFrameFromHW(AVPacket* pkt, uint8_t* output, int* outputSize, bool* isKeyFrame);

private:
    bool m_bInitialized;
    HWAccelType m_currentHWType;
    
    AVCodecContext* m_codecCtx;        // 编码器上下文
    AVBufferRef* m_hwDeviceCtx;         // 硬件设备上下文
    AVFrame* m_hwFrame;                 // 硬件帧
    AVFrame* m_swFrame;                 // 软件帧（用于上传到硬件）
    AVBSFContext* m_bsfCtx;             // Bitstream filter上下文（用于AVCC转Annex-B）
    
    EncoderParams m_params;             // 编码器参数
    
    int m_frameCount;                  // 帧计数（用于GOP控制）
};

#endif // HWVIDEOENCODER_H


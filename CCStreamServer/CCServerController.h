#ifndef CCSERVERCONTROLLER_H
#define CCSERVERCONTROLLER_H

#include "H264Capture.h"
#include "CCStreamServer.h"
#include "FFmpegRTMPSender.h"
#include "HWVideoEncoder.h"
#include "BeautyFilter.h"
#include "WebSocketControlServer.h"
#include "ALSAAudioCapture.h"
#include "AACEncoder.h"
#include "ThreadPool.h"
#include <vector>
#include <string>
#include <mutex>

/**
 * @brief 智能直播服务器控制器
 * 集成视频采集、硬件编码、美颜滤镜、RTMP推流、WebSocket控制等功能
 */
class CCServerController
{
public:
    CCServerController();
    ~CCServerController();

    void StartServerController();
    void RunVideoCapture();
    

private:
    void initVideoCapture();
    void unInitVideoCapture();
    
    /**
     * @brief 初始化音频采集
     */
    void initAudioCapture();
    
    /**
     * @brief 反初始化音频采集
     */
    void unInitAudioCapture();
    
    /**
     * @brief 初始化RTMP推流
     */
    void initRTMPStreaming();
    
    /**
     * @brief 初始化硬件编码器
     */
    void initHWEncoder();
    
    /**
     * @brief 初始化美颜滤镜
     */
    void initBeautyFilter();
    
    /**
     * @brief 初始化WebSocket控制服务器
     */
    void initWebSocketServer();
    
    /**
     * @brief 处理WebSocket控制命令
     */
    void handleWebSocketCommand(const std::string& jsonData);

    static void startCaptureThread(long long userData);
    static void serverExitSignalProcess(int num);

private:
    static bool            m_bServerRunning;
    IOTC_Camera*    m_pCamera;

    CCStreamServer* m_pTCPServer;              // TCP服务器（向后兼容）
    ThreadPool* m_pRecvThreadPool;             // 接收任务线程池
    ThreadPool* m_pSendThreadPool;             // 发送任务线程池
    
    FFmpegRTMPSender* m_pRTMPSender;          // RTMP推流器
    HWVideoEncoder* m_pHWEncoder;              // 硬件编码器
    BeautyFilter* m_pBeautyFilter;             // 美颜滤镜
    WebSocketControlServer* m_pWebSocketServer; // WebSocket控制服务器
    ALSAAudioCapture* m_pAudioCapture;        // ALSA音频采集
    AACEncoder* m_pAACEncoder;                 // AAC编码器
    
    // 美颜参数（可通过WebSocket动态调整）
    BeautyFilterParams m_beautyParams;
    std::mutex m_beautyParamsMutex;
    
    // RTMP推流地址列表
    std::vector<std::string> m_rtmpUrls;
    std::mutex m_rtmpUrlsMutex;
    
    // 推流状态
    bool m_bStreaming;
    
    // 临时缓冲区（用于美颜处理）
    uint8_t* m_pBeautyBuffer;
    int m_beautyBufferSize;
    
    // 音频相关
    uint8_t* m_pPCMBuffer;                     // PCM缓冲区
    uint8_t* m_pAACBuffer;                    // AAC缓冲区
    int m_pcmBufferSize;
    int m_aacBufferSize;
    int64_t m_audioFrameCount;                // 音频帧计数（用于时间戳）
    int64_t m_audioStartTime;                 // 音频开始时间
};

#endif
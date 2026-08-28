#ifndef CCVIDEOCLIENT_H
#define CCVIDEOCLIENT_H


#include <mutex>
#include <thread>
#include <atomic>
#include <iostream>
#include <queue>
#include <condition_variable>
#include <chrono>

#include "CCSocketDefine.h"
#include "h264decoder.h"
#include "CCYUVDataDefine.h"

typedef void (UpdateVideo2GUI_Callback) (YUVData_Frame* yuvData, unsigned long userData);// 定义回调函数类型：返回void，参数为YUV数据指针和用户数据
typedef void (UpdateAudio2GUI_Callback) (uint8_t* pcmData, int pcmSize, unsigned long userData);// 定义音频回调函数类型：返回void，参数为PCM数据指针、大小和用户数据

// 音视频帧数据结构（带时间戳）
struct VideoFrame {
    YUVData_Frame yuvFrame;
    int64_t timestamp;  // 时间戳（毫秒）
};

struct AudioFrame {
    uint8_t* pcmData;
    int pcmSize;
    int64_t timestamp;  // 时间戳（毫秒）
};

class CCVideoClient
{
public:
    CCVideoClient();
    ~CCVideoClient();

    void StartSocketConnection(CC_NetConnectInfo* netInfo);
    void StopSocketClient();

    void SetupUpdateGUICallback(UpdateVideo2GUI_Callback callback, unsigned long userData);//
    void SetupUpdateAudioCallback(UpdateAudio2GUI_Callback callback, unsigned long userData);//
    
private:
    //static void doWaitConnectionThread(long long userData);
    //static void startRecvStreamThread(long long userData);
    // static void clientExitSignalProcess(int num);
    void sendKeepAlivePacketThread();
    void runWaitConnectionThread();//3 Thread
    void runRecvAVStreamThread();
    void runAVSyncThread();  // 音视频同步线程


    //void closeSocketConnection();

    bool recvSocketData(char* pBuff, unsigned int length);
    bool sendSocketData(char* pBuff, unsigned int length);
    
    // 音视频同步相关函数
    void ProcessVideoFrame(YUVData_Frame* yuvFrame, int64_t timestamp);
    void ProcessAudioFrame(uint8_t* pcmData, int pcmSize, int64_t timestamp);
    void SyncAVFrames();  // 同步音视频帧
    int64_t GetCurrentTime();  // 获取当前时间（毫秒）

private:
    int m_sockfd;
    bool m_bConnected;

    std::atomic_bool m_bThreadRuning;

    std::atomic_bool m_bKeepAliveThreadStatus;
    std::atomic_bool m_bRecvAVThreadStatus;
    std::atomic_bool m_bSyncThreadStatus;  // 同步线程状态

    std::mutex m_recvMutex;
    std::mutex m_sendMutex;
    std::mutex m_videoQueueMutex;  // 视频队列互斥锁
    std::mutex m_audioQueueMutex;  // 音频队列互斥锁
    std::mutex m_syncMutex;        // 同步互斥锁

    UpdateVideo2GUI_Callback* m_updateCallback;//存储回调函数指针
    unsigned long m_userData;// 存储用户数据（如MainWindow指针）
    
    UpdateAudio2GUI_Callback* m_updateAudioCallback;//存储音频回调函数指针
    
    // 音视频同步相关
    std::queue<VideoFrame> m_videoQueue;  // 视频帧队列
    std::queue<AudioFrame> m_audioQueue;  // 音频帧队列
    int64_t m_startTime;                  // 播放开始时间（毫秒）
    int64_t m_audioClock;                 // 音频时钟（毫秒）
    int64_t m_videoClock;                 // 视频时钟（毫秒）
    bool m_bSyncStarted;                  // 同步是否已开始
    static const int MAX_QUEUE_SIZE = 30;  // 最大队列长度
    static const int SYNC_THRESHOLD = 50;   // 同步阈值（毫秒）

};

#endif

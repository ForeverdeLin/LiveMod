#ifndef CCCLIENTPROCESS_H
#define CCCLIENTPROCESS_H

#include "CCStreamServerDef.h"
#include "JCQueueDef.h"

class ThreadPool;

class CCClientProcess
{
public:
    CCClientProcess();
    ~CCClientProcess();
    // 使用线程池启动客户端处理
    void StartClientProcessWithSockfd(int sockfd, ThreadPool* recvPool, ThreadPool* sendPool);
    // 兼容旧接口（不使用线程池）
    void StartClientProcessWithSockfd(int sockfd);

    void RunRecvProcess();
    void RunSendProcess();
    
    // 单次接收任务（供线程池调用）
    void RunRecvTaskOnce();
    // 单次发送任务（供线程池调用）
    void RunSendTaskOnce();

    bool GetClientRunningStatus();

    //void StopClientRunning();
    void EnqueueAVStream(uint8_t* pBuff, uint32_t buffSize, uint16_t sType, int64_t timestamp = 0);

    void freeAllRemainAVStream();

private:
    static void startRecvThread(long long userData);
    static void startSendThread(long long userData);
    bool sendSocketData(char* pBuff, unsigned int ulength);
    bool recvSocketData(char* pBuff, unsigned int ulength);

    void closeListenSockfd();

    void disableClientRunningStatus();
    
    // 提交接收任务到线程池（循环提交）
    void SubmitRecvTask();
    // 提交发送任务到线程池（循环提交）
    void SubmitSendTask();
    
private:
    int m_clientSockfd;
    std::atomic_bool m_bClientRunning;
    pthread_mutex_t m_recvMutex;
    pthread_mutex_t m_sendMutex;

    JCMediaQueue <CC_AVStream*> m_avStreamQueue;//每一个客户端维护一个队列
    
    ThreadPool* m_pRecvThreadPool;    // 接收任务线程池
    ThreadPool* m_pSendThreadPool;   // 发送任务线程池
    std::atomic_bool m_bUseThreadPool;  // 是否使用线程池
    
    int m_keepAliveCount;            // 心跳计数器（用于接收任务）


};

#endif
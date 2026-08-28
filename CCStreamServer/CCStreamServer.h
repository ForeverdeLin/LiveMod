#ifndef CCSTREAMSERVER_H
#define CCSTREAMSERVER_H

#include "CCStreamServerDef.h"
#include "CCClientProcess.h"

class ThreadPool;

class CCStreamServer
{
public:
    CCStreamServer(ThreadPool* recvPool = nullptr, ThreadPool* sendPool = nullptr);
    ~CCStreamServer();

    void StartTCPServer();
    static void StopServerExec();
    void SendAVStream(uint8_t* pBuff, uint32_t len, uint16_t type, int64_t timestamp = 0);

private:
    void closeListenSockfd();
    //static void serverExitSignalProcess(int num);
    void checkClientRunningStatus();



private:

    int            m_listenSockfd;
    static bool    m_bServerRunning;//静态初始化怎么初始化

    std::vector<CCClientProcess*> m_clients;
    ThreadPool* m_pRecvThreadPool;    // 接收任务线程池
    ThreadPool* m_pSendThreadPool;    // 发送任务线程池

};

#endif
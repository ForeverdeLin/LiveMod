#ifndef CCVIDEOCLIENT_H
#define CCVIDEOCLIENT_H

#include "CCSocketDefine.h"

/**
 * @brief 视频客户端（TCP模式）
 * 通过TCP连接到服务器接收H.264视频流并录制为MP4
 */
class CCVideoClient
{
public:
    CCVideoClient();
    ~CCVideoClient();

    /**
     * @brief 启动TCP连接
     * @param netInfo 网络连接信息
     */
    void StartSocketConnection(CC_NetConnectInfo* netInfo);
    
    void RunWaitConnection();
    void RunRecvAVStream();
    
private:
    static void doWaitConnectionThread(long long userData);

    static void clientExitSignalProcess(int num);
    void sendKeepAlivePacket();

    static void startRecvStreamThread(long long userData);

    void closeSocketConnection();

    bool recvSocketData(char* pBuff, unsigned int length);
    bool sendSocketData(char* pBuff, unsigned int length);

private:
    int m_sockfd;
    bool m_bConnected;

    static bool m_bThreadRuning;
    pthread_mutex_t m_recvMutex;
    pthread_mutex_t m_sendMutex;

};

#endif

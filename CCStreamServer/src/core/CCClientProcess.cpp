/**
 * @file CCClientProcess.cpp
 * @brief 客户端处理进程实现
 * @details 管理单个TCP客户端的连接，负责接收心跳和发送音视频流
 *          知识点：多线程编程、消息队列、TCP Socket通信
 *          支持线程池模式：使用线程池管理任务，避免为每个客户端创建线程
 */

#include "CCClientProcess.h"
#include "CCThread.h"
#include "ThreadPool.h"

/**
 * @brief 构造函数
 * @details 初始化客户端处理对象
 *          知识点：pthread互斥锁初始化
 */
CCClientProcess::CCClientProcess()
    : m_clientSockfd(-1)                    // 客户端socket文件描述符（未连接）
    , m_bClientRunning(true)                 // 客户端运行标志：运行中
    , m_pRecvThreadPool(nullptr)            // 接收线程池（默认不使用）
    , m_pSendThreadPool(nullptr)            // 发送线程池（默认不使用）
    , m_bUseThreadPool(false)               // 默认不使用线程池
    , m_keepAliveCount(5)                   // 心跳计数器初始值
{
    pthread_mutex_init(&m_recvMutex,NULL); // 初始化接收线程互斥锁
    pthread_mutex_init(&m_sendMutex,NULL); // 初始化发送线程互斥锁
}

CCClientProcess::~CCClientProcess()
{
    printf("\nClientProcess-die: sockfd:%d\n",m_clientSockfd);
    closeListenSockfd();

    freeAllRemainAVStream();
}

/**
 * @brief 启动客户端处理进程（使用线程池）
 * @details 使用线程池管理客户端任务，避免为每个客户端创建线程
 * @param sockfd 客户端socket文件描述符
 * @param recvPool 接收任务线程池
 * @param sendPool 发送任务线程池
 */
void CCClientProcess::StartClientProcessWithSockfd(int sockfd, ThreadPool* recvPool, ThreadPool* sendPool)
{
    m_clientSockfd = sockfd;
    m_pRecvThreadPool = recvPool;
    m_pSendThreadPool = sendPool;
    m_bUseThreadPool = (recvPool != nullptr && sendPool != nullptr);
    m_keepAliveCount = 5;
    
    if (m_bUseThreadPool) {
        // 使用线程池：提交任务到线程池
        SubmitRecvTask();  // 启动接收任务循环
        SubmitSendTask();  // 启动发送任务循环
    } else {
        // 兼容模式：创建独立线程（向后兼容）
        detach_thread_create(NULL, (void *)startRecvThread, (void *)this);
        detach_thread_create(NULL, (void *)startSendThread, (void *)this);
    }
}

/**
 * @brief 启动客户端处理进程（兼容旧接口，不使用线程池）
 */
void CCClientProcess::StartClientProcessWithSockfd(int sockfd)
{
    StartClientProcessWithSockfd(sockfd, nullptr, nullptr);
}

/**
 * @brief 接收线程入口函数（静态成员函数）
 * @param userData CCClientProcess对象指针
 */
void CCClientProcess::startRecvThread(long long userData)
{
    CCClientProcess* clientPtr = (CCClientProcess*)userData;
    if(clientPtr != NULL){
        clientPtr->RunRecvProcess();
    }
}

/**
 * @brief 接收线程主循环
 * @details 知识点：心跳机制
 *          1. 每2秒检查一次心跳包
 *          2. 如果10秒（5次）未收到心跳，认为客户端断开
 *          3. 收到心跳包后重置计数器
 *          
 *          心跳包格式：
 *          - headerID: "CCTC"（4字节标识）
 *          - msgType: MSGHEADER_TYPE_KEEPALIVE（心跳类型）
 */
void CCClientProcess::RunRecvProcess()
{
    int keepAliveCount=5;  // 心跳计数器（5次 * 2秒 = 10秒超时）
    while(m_bClientRunning)
    {
        usleep(2000*1000);  // 休眠2秒
        keepAliveCount--;
        
        // 如果10秒未收到心跳，认为客户端断开
        if(keepAliveCount<=0)
        {
            disableClientRunningStatus();  // 设置运行标志为false
            break;
        }

        // 知识点：接收消息头
        // 尝试接收消息头（非阻塞，如果无数据会立即返回）
        CC_MsgHeader msgHeader;
        memset((char*)&msgHeader,0,sizeof(CC_MsgHeader));
        recvSocketData((char*)&msgHeader,sizeof(CC_MsgHeader));

        // 检查消息头标识和类型
        if(strcmp((char*)msgHeader.headerID,"CCTC") == 0)
        {
            if(msgHeader.msgType == MSGHEADER_TYPE_KEEPALIVE)
            {
                keepAliveCount=5;  // 收到心跳，重置计数器
            }
        }
    }
    printf("\n recv process exit\n");
}

/**
 * @brief 发送线程入口函数（静态成员函数）
 * @details 知识点：静态成员函数作为线程入口
 *          静态函数不能直接访问非静态成员，需要通过参数传递对象指针
 * @param userData CCClientProcess对象指针
 */
void CCClientProcess::startSendThread(long long userData)
{
    CCClientProcess* clientPtr = (CCClientProcess*)userData;
    if(clientPtr != NULL){
        clientPtr->RunSendProcess();
    }
}

/**
 * @brief 发送线程主循环
 * @details 知识点：消息队列 + TCP发送
 *          1. 从消息队列取音视频数据
 *          2. 构造消息头（包含数据类型和大小）
 *          3. 先发送消息头，再发送数据
 *          4. 释放数据缓冲区
 *          
 *          消息格式：
 *          [消息头] + [数据]
 *          消息头：headerID("CCTC") + msgType + subType(视频/音频) + length
 */
void CCClientProcess::RunSendProcess()
{
    while (m_bClientRunning)
    {
        // 知识点：消息队列检查
        // 如果队列为空，休眠10ms后继续检查（避免CPU空转）
        if(m_avStreamQueue.isEmpty()){
            usleep(10*1000);
            continue;
        }

        // 从队列中取出一个音视频流数据
        CC_AVStream* pAVStream = (CC_AVStream*)m_avStreamQueue.dequeue();
        if((pAVStream != NULL) && (pAVStream->buffer != NULL) && (pAVStream->size > 0))
        {
            // 知识点：构造消息头
            // 消息头包含：标识、类型、子类型（视频/音频）、数据长度、时间戳
            CC_MsgHeader msgHeader;
            memset((char*)&msgHeader,0,sizeof(CC_MsgHeader));  // 清零
            strncpy(msgHeader.headerID,"CCTC",sizeof(msgHeader.headerID));  // 消息标识
            msgHeader.msgType = MSGHEADER_TYPE_AVSTREAM;        // 消息类型：音视频流
            msgHeader.subType = pAVStream->type;                // 子类型：视频或音频
            msgHeader.length = pAVStream->size;                 // 数据长度（字节）
            msgHeader.timestamp = pAVStream->timestamp;         // 时间戳（毫秒），用于音视频同步

            // 知识点：TCP发送流程
            // 1. 先发送消息头（固定大小，便于接收方解析）
            sendSocketData((char*)&msgHeader,sizeof(msgHeader));
            
            // 2. 再发送实际数据（音视频编码数据）
            sendSocketData((char*)pAVStream->buffer,pAVStream->size);

            // 知识点：释放数据缓冲区
            // 数据已发送到TCP协议栈的发送缓冲区，可以释放用户缓冲区
            // 注意：TCP是可靠传输，数据会由协议栈保证送达
            free(pAVStream->buffer);
            delete pAVStream;
        }
    }

    printf("\n send process exit...%d\n",m_clientSockfd);

}


bool CCClientProcess::GetClientRunningStatus()
{
    return m_bClientRunning;
}

void CCClientProcess::disableClientRunningStatus()
{
    m_bClientRunning=false;
}





bool CCClientProcess::recvSocketData(char* pBuff, unsigned int uLength)
{
    if(!m_bClientRunning)
    {
        return false;
    }
    signal(SIGPIPE, SIG_IGN);

    pthread_mutex_lock(&m_recvMutex);

    int recvLen=0;
    int nRet=0;

    while(recvLen < uLength)
    {
        nRet=recv(m_clientSockfd,pBuff,uLength-recvLen,0);

        if(nRet<0)
        {
            if(errno==EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
            {
                usleep(10*1000);
                continue;
            }
            pthread_mutex_unlock(&m_recvMutex);
            printf("socket recv error,nRet<0\n");
            return false;

        }

        if( 0==nRet)
        {
            pthread_mutex_unlock(&m_recvMutex);
            printf("socket recv exit,nRet=0 客户exit\n");
            disableClientRunningStatus();
            return false;
        }
        recvLen += nRet;
        pBuff += nRet;
    }

    pthread_mutex_unlock(&m_recvMutex);
    return true;
}


bool CCClientProcess::sendSocketData(char* pBuff, unsigned int ulength)
{
    if(!m_bClientRunning)
    {
        return false;
    }
    signal(SIGPIPE, SIG_IGN);//作用是将 SIGPIPE 信号的处理动作设置为 “忽略”

    pthread_mutex_lock(&m_sendMutex);

    int sendLen=0;
    int nRet=0;

    while(sendLen<ulength)
    {
        nRet=send(m_clientSockfd,pBuff,ulength-sendLen,0);

        if(nRet < 0)
        {
            if(errno==EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
            {
                usleep(10*1000);
                continue;
            }
            pthread_mutex_unlock(&m_sendMutex);
            //printf("Socket send error\n");
            return false;            
        }

        if(0 == nRet)
        {
            pthread_mutex_unlock(&m_sendMutex);
            printf("Socket send error:ret=0\n");
            //disableClientRunningStatus();
            return false;
        }
        sendLen += nRet;
        pBuff += nRet;
    }

    //printf("SEND LEN: %d %d\n",ulength,sendLen);

    pthread_mutex_unlock(&m_sendMutex);
    return true;
}


void CCClientProcess::EnqueueAVStream(uint8_t* pBuff, uint32_t buffSize, uint16_t sType, int64_t timestamp)
{
    if(m_avStreamQueue.size() > 50){//防止采集爆占内存
        return;
    }

    CC_AVStream* pAVStream = new CC_AVStream();//new

    pAVStream->buffer = (uint8_t*)malloc(buffSize);//malloc混用，什么场景用new
    memcpy(pAVStream->buffer, pBuff, buffSize);
    pAVStream->size = buffSize;
    pAVStream->type = sType;
    pAVStream->timestamp = timestamp;  // 存储时间戳，用于音视频同步

    m_avStreamQueue.enqueue(pAVStream);//一次可能是一帧对应的264？可能不止一帧，那是怎么处理可能不是完整的数据包的？
}



void CCClientProcess::freeAllRemainAVStream()
{
    

    while(!m_avStreamQueue.isEmpty())
    {
        printf("\nfreeAllRemainAVStream: %ld\n...",m_avStreamQueue.size());
        CC_AVStream* pAVStream = (CC_AVStream*)m_avStreamQueue.dequeue();
    if((pAVStream != NULL) && (pAVStream->buffer != NULL))
    {
        free(pAVStream->buffer);
        delete pAVStream;
    }
    }
}



void CCClientProcess::closeListenSockfd()
{
    if( m_clientSockfd ){
        close(m_clientSockfd);
        m_clientSockfd = -1;
    }
}

/**
 * @brief 提交接收任务到线程池（循环提交）
 * @details 每次执行完接收任务后，如果客户端还在运行，继续提交下一个任务
 */
void CCClientProcess::SubmitRecvTask()
{
    if (!m_bClientRunning || !m_pRecvThreadPool) {
        return;
    }
    
    // 将接收任务提交到线程池
    m_pRecvThreadPool->EnqueueTask([this]() {
        RunRecvTaskOnce();
        
        // 如果还在运行，继续提交下一个接收任务
        if (m_bClientRunning) {
            SubmitRecvTask();
        }
    });
}

/**
 * @brief 单次接收任务（供线程池调用）
 * @details 执行一次接收操作，检查心跳包
 */
void CCClientProcess::RunRecvTaskOnce()
{
    if (!m_bClientRunning) {
        return;
    }
    
    // 检查心跳超时
    m_keepAliveCount--;
    if (m_keepAliveCount <= 0) {
        disableClientRunningStatus();
        return;
    }
    
    // 尝试接收消息头（非阻塞）
    CC_MsgHeader msgHeader;
    memset((char*)&msgHeader, 0, sizeof(CC_MsgHeader));
    
    if (recvSocketData((char*)&msgHeader, sizeof(CC_MsgHeader))) {
        // 检查消息头标识和类型
        if (strcmp((char*)msgHeader.headerID, "CCTC") == 0) {
            if (msgHeader.msgType == MSGHEADER_TYPE_KEEPALIVE) {
                m_keepAliveCount = 5;  // 收到心跳，重置计数器
            }
        }
    }
    
    // 休眠2秒（模拟原来的循环间隔）
    usleep(2000 * 1000);
}

/**
 * @brief 提交发送任务到线程池（循环提交）
 * @details 每次执行完发送任务后，如果客户端还在运行，继续提交下一个任务
 */
void CCClientProcess::SubmitSendTask()
{
    if (!m_bClientRunning || !m_pSendThreadPool) {
        return;
    }
    
    // 将发送任务提交到线程池
    m_pSendThreadPool->EnqueueTask([this]() {
        RunSendTaskOnce();
        
        // 如果还在运行，继续提交下一个发送任务
        if (m_bClientRunning) {
            SubmitSendTask();
        }
    });
}

/**
 * @brief 单次发送任务（供线程池调用）
 * @details 从队列中取一个数据包发送
 */
void CCClientProcess::RunSendTaskOnce()
{
    if (!m_bClientRunning) {
        return;
    }
    
    // 如果队列为空，休眠10ms后返回（下次任务会继续检查）
    if (m_avStreamQueue.isEmpty()) {
        usleep(10 * 1000);
        return;
    }
    
    // 从队列中取出一个音视频流数据
    CC_AVStream* pAVStream = (CC_AVStream*)m_avStreamQueue.dequeue();
    if ((pAVStream != NULL) && (pAVStream->buffer != NULL) && (pAVStream->size > 0)) {
        // 构造消息头
        CC_MsgHeader msgHeader;
        memset((char*)&msgHeader, 0, sizeof(CC_MsgHeader));
        strncpy(msgHeader.headerID, "CCTC", sizeof(msgHeader.headerID));
        msgHeader.msgType = MSGHEADER_TYPE_AVSTREAM;
        msgHeader.subType = pAVStream->type;
        msgHeader.length = pAVStream->size;
        msgHeader.timestamp = pAVStream->timestamp;  // 时间戳（毫秒），用于音视频同步
        
        // 发送消息头和数据
        sendSocketData((char*)&msgHeader, sizeof(msgHeader));
        sendSocketData((char*)pAVStream->buffer, pAVStream->size);
        
        // 释放数据缓冲区
        free(pAVStream->buffer);
        delete pAVStream;
    }
}




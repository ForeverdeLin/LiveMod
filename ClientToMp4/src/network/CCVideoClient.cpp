/**
 * @file CCVideoClient.cpp
 * @brief TCP视频客户端实现
 * @details 从流媒体服务器TCP连接接收H.264/AAC数据并录制为MP4
 *          知识点：TCP Socket客户端、多线程接收、MP4录制
 */

#include "CCVideoClient.h"
#include "CCThread.h"
#include "CCVideoWriter.h"

// 静态成员变量初始化：线程运行标志
bool CCVideoClient::m_bThreadRuning=true;

/**
 * @brief 构造函数
 * @details 初始化TCP客户端对象
 *          知识点：pthread互斥锁初始化、信号处理
 */
CCVideoClient::CCVideoClient()
{
    m_sockfd=-1;  // Socket文件描述符（未连接）
    
    // 知识点：注册信号处理函数
    // SIGINT: 中断信号（Ctrl+C），用于优雅退出
    signal(SIGINT,clientExitSignalProcess);
    
    // 知识点：初始化互斥锁
    // 用于保护接收和发送操作的线程安全
    pthread_mutex_init(&m_recvMutex,NULL);
    pthread_mutex_init(&m_sendMutex,NULL);

    m_bConnected=false;  // 连接状态：未连接
    printf("init\n");
}

/**
 * @brief 析构函数
 * @details 清理资源，停止录制并关闭Socket连接
 *          功能：停止MP4写入、关闭Socket连接、等待线程退出
 */
CCVideoClient::~CCVideoClient()
{
    printf("\ndestroy\n");
    CCVideoWriter::GetInstance()->StopWriteReleaseResources();
    closeSocketConnection();
    usleep(3000*1000);
}

/**
 * @brief 启动Socket连接
 * @details 知识点：TCP客户端连接流程
 *          1. 创建Socket（socket）
 *          2. 配置服务器地址（sockaddr_in）
 *          3. 设置非阻塞模式（fcntl：O_NONBLOCK）
 *          4. 发起连接（connect）
 *          5. 创建等待连接线程（doWaitConnectionThread）
 *          6. 创建接收流线程（startRecvStreamThread）
 *          7. 启动MP4录制
 *          8. 发送心跳包
 * @param netInfo 网络连接信息（服务器IP和端口）
 */
void CCVideoClient::StartSocketConnection(CC_NetConnectInfo* netInfo)
{
    printf("Start socket connection IP: %s port: %d\n",netInfo->server_ip,netInfo->port);

    // 知识点：创建TCP Socket
    // socket - 创建套接字
    // AF_INET: IPv4地址族
    // SOCK_STREAM: TCP协议
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd < 0) {
        printf("socket error! \n");
        return;
    }

    // 知识点：配置服务器地址结构
    struct sockaddr_in sockAddrIn;
    memset(&sockAddrIn, 0, sizeof(struct sockaddr_in));

    sockAddrIn.sin_family = AF_INET;                              // 地址族：IPv4
    sockAddrIn.sin_port = htons(netInfo->port);                   // 端口号（主机字节序转网络字节序）
    sockAddrIn.sin_addr.s_addr = htons(INADDR_ANY);               // 本地地址（客户端不需要绑定）

    // 知识点：IP地址转换
    // inet_pton - 将点分十进制IP字符串转换为网络字节序的二进制地址
    if (inet_pton(AF_INET, netInfo->server_ip, &sockAddrIn.sin_addr.s_addr) < 0) {
        printf("inet_pton error!\n");
        return;
    }

    // 知识点：设置非阻塞模式
    // fcntl - 文件控制操作
    // O_NONBLOCK: 非阻塞模式（connect不会阻塞）
    int flags = fcntl(m_sockfd, F_GETFL, 0);
    fcntl(m_sockfd, F_SETFL, flags | O_NONBLOCK);

    // 知识点：发起TCP连接
    // connect - 连接到服务器（非阻塞，立即返回）
    // 返回：0=成功，-1=失败（EINPROGRESS表示正在连接中）
    int ret = connect(m_sockfd, (struct sockaddr*)&sockAddrIn, sizeof(struct sockaddr));
    printf("socket connection result: %d\n", ret);    

    // 知识点：创建等待连接线程
    // 由于使用非阻塞connect，需要单独线程等待连接完成
    detach_thread_create(NULL, (void*)doWaitConnectionThread, (void*)this);
    
    // 知识点：创建接收流线程
    // 接收服务器发送的音视频流数据
    detach_thread_create(NULL, (void*)startRecvStreamThread, (void*)this);

    // 知识点：启动MP4录制
    // CCVideoWriter - 单例模式的MP4写入器
    // SetVideoSize - 设置视频分辨率（需要根据实际流调整）
    CCVideoWriter::GetInstance()->SetVideoSize(640, 360);
    // StartVideoWriterWithPath - 创建MP4文件并开始写入
    CCVideoWriter::GetInstance()->StartVideoWriterWithPath("./20211115.mp4");

    // 知识点：发送心跳包
    // 保持TCP连接活跃，防止服务器断开
    sendKeepAlivePacket();
    return ;
}

/**
 * @brief 等待连接线程入口函数
 * @details 静态函数，作为线程入口点，调用实例的RunWaitConnection方法
 *          功能：将线程参数转换为CCVideoClient指针并执行连接等待逻辑
 * @param userData CCVideoClient对象指针
 */
void CCVideoClient::doWaitConnectionThread(long long userData)
{
    CCVideoClient* clientPtr = (CCVideoClient*)userData;
    if(clientPtr != NULL){
        clientPtr->RunWaitConnection();
    }
}

/**
 * @brief 等待连接完成
 * @details 知识点：非阻塞connect的完成检测
 *          使用select检测socket可写事件，判断connect是否成功
 *          
 *          非阻塞connect的特点：
 *          - connect立即返回-1，errno=EINPROGRESS（正在连接）
 *          - 使用select检测socket可写，表示连接完成
 *          - 使用getsockopt检查SO_ERROR，判断连接是否成功
 */
void CCVideoClient::RunWaitConnection()
{
    int error=0;
    fd_set r_set,w_set;  // 文件描述符集合（用于select）
    FD_ZERO(&w_set);     // 清零写集合
    FD_ZERO(&r_set);     // 清零读集合

    // 知识点：将socket加入select集合
    // FD_SET - 将文件描述符加入集合
    // 将socket加入读集合和写集合，用于检测连接状态
    FD_SET(m_sockfd,&r_set);
    FD_SET(m_sockfd,&w_set);
    struct timeval timeout={0,0};  // 超时时间（0=立即返回）

    // 知识点：使用select检测socket状态
    // select - 检测文件描述符是否可读/可写
    // 对于非阻塞connect：
    //   - 如果socket可写，说明connect完成（成功或失败）
    //   - 需要进一步检查SO_ERROR判断是否成功
    int retValue=select(m_sockfd+1, &r_set, &w_set, NULL, &timeout);
    switch (retValue)
    {
        case -1:
        {
            printf("select 系统调用出错\n");
            return ;
            break;
        }
        case 0://select超时
        {
            printf("select超时...\n");
            return ;
            break;
        }
        default:
        {
            //套接字即可读又可写,需要进一步判断。
            if(FD_ISSET(m_sockfd,&r_set) && FD_ISSET(m_sockfd,&w_set))//分别检查 r_set 和 w_set 这两个位集合中，
            //与 m_sockfd 数值对应的那一位是否被设置（即是否为 1）。
            {
                //如果套接口及可写也可读,需要进一步判断
                socklen_t len = sizeof(error);
                if(getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0){
                    return ;//获取SO_ERROR属性选项,当然getsockopt也有可能错误返回
                }
                printf("error = %d\n", error);
                if(error != 0)
                {
                    //如果error不为0，则表示链接到此处有建立完成
                    printf("connect failed\n");
                    return ;
                }
                //如果error为0，则说明链接建立完成
            }
            //如果套按字可写但是不可读,说明连接完成
            if(!FD_ISSET(m_sockfd,&r_set) && FD_ISSET(m_sockfd,&w_set))
            {
                //如果套接口可写不可读,则链接完成
                printf("connect success\n");


                m_bConnected=true;
            }
            break;
        }
    }
}


/**
 * @brief 接收Socket数据
 * @details 线程安全的Socket数据接收函数，确保接收完整的数据包
 *          功能：循环接收直到接收完指定长度的数据，处理非阻塞模式下的EAGAIN错误
 *          知识点：pthread互斥锁保护、非阻塞recv、部分接收处理
 * @param pBuff 接收缓冲区指针
 * @param uLength 需要接收的数据长度
 * @return true=成功接收完整数据，false=接收失败
 */
bool CCVideoClient::recvSocketData(char* pBuff, unsigned int uLength)
{
    signal(SIGPIPE, SIG_IGN);

    pthread_mutex_lock(&m_recvMutex);

    int recvLen=0;
    int nRet=0;

    while(recvLen < uLength)
    {
        nRet=recv(m_sockfd,pBuff,uLength-recvLen,0);

        if(nRet<0)
        {
            if(errno==EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
            {
                usleep(10*1000);
                continue;
            }
            pthread_mutex_unlock(&m_recvMutex);
            //printf("socket recv error\n");
            return false;

        }

        if( 0==nRet)
        {
            pthread_mutex_unlock(&m_recvMutex);
            printf("socket recv error ret=0\n");
            return false;
        }
        recvLen += nRet;
        pBuff += nRet;
    }

    pthread_mutex_unlock(&m_recvMutex);
    return true;
}


/**
 * @brief 发送Socket数据
 * @details 线程安全的Socket数据发送函数，确保发送完整的数据包
 *          功能：循环发送直到发送完指定长度的数据，处理非阻塞模式下的EAGAIN错误
 *          知识点：pthread互斥锁保护、非阻塞send、部分发送处理、SIGPIPE信号忽略
 * @param pBuff 发送缓冲区指针
 * @param ulength 需要发送的数据长度
 * @return true=成功发送完整数据，false=发送失败
 */
bool CCVideoClient::sendSocketData(char* pBuff, unsigned int ulength)
{
    signal(SIGPIPE, SIG_IGN);//作用是将 SIGPIPE 信号的处理动作设置为 "忽略"

    pthread_mutex_lock(&m_sendMutex);

    int sendLen=0;
    int nRet=0;

    while(sendLen<ulength)
    {
        nRet=send(m_sockfd,pBuff,ulength-sendLen,0);

        if(nRet < 0)
        {
            if(errno==EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
            {
                usleep(10*1000);
                continue;
            }
            pthread_mutex_unlock(&m_sendMutex);
            printf("Socket send error\n");
            return false;            
        }

        if(0 == nRet)
        {
            pthread_mutex_unlock(&m_sendMutex);
            printf("Socket send error\n");
            return false;
        }
        sendLen += nRet;
        pBuff += nRet;
    }

    printf("SEND LEN: %d %d\n",ulength,sendLen);

    pthread_mutex_unlock(&m_sendMutex);
    return true;
}

/**
 * @brief 客户端退出信号处理函数
 * @details 处理SIGINT信号（Ctrl+C），设置线程运行标志为false，实现优雅退出
 *          功能：当用户按下Ctrl+C时，停止所有线程的运行循环
 * @param num 信号编号（SIGINT）
 */
void CCVideoClient::clientExitSignalProcess(int num)
{
    m_bThreadRuning=false;
    printf("process exit by SIGINT...\n");
}


/**
 * @brief 发送心跳包
 * @details 定期发送心跳包保持TCP连接活跃，防止服务器因超时断开连接
 *          功能：每2秒发送一次心跳包（MSGHEADER_TYPE_KEEPALIVE），仅在连接建立后发送
 *          知识点：心跳机制、TCP连接保活、线程循环
 */
void CCVideoClient::sendKeepAlivePacket()
{
  
    while(m_bThreadRuning)
    {
        usleep(2000*1000);
        printf("send keep alive packet...\n");
        if(m_bConnected==false)
        {
            continue;
        }
        CC_MsgHeader msgHeader;
        memset((char*)&msgHeader,0,sizeof(CC_MsgHeader));

        strncpy(msgHeader.headerID,"CCTC",sizeof(msgHeader.headerID));
        msgHeader.msgType = MSGHEADER_TYPE_KEEPALIVE;
        msgHeader.length = 0;

        sendSocketData((char*)&msgHeader,sizeof(CC_MsgHeader));
    }

}

/**
 * @brief 接收流线程入口函数
 * @details 静态函数，作为线程入口点，调用实例的RunRecvAVStream方法
 *          功能：将线程参数转换为CCVideoClient指针并执行音视频流接收逻辑
 * @param userData CCVideoClient对象指针
 */
void CCVideoClient::startRecvStreamThread(long long userData)//为什么要多这一步
{
    CCVideoClient* clientPtr = (CCVideoClient*)userData;
    if(clientPtr != NULL){
        clientPtr->RunRecvAVStream();
    }
}




/**
 * @brief 接收音视频流
 * @details 主接收循环，持续接收服务器发送的音视频数据并写入MP4文件
 *          功能：接收消息头、验证消息格式、根据消息类型分发到视频/音频写入器
 *          知识点：TCP数据包解析、消息头验证、音视频数据分离、MP4写入
 */
void CCVideoClient::RunRecvAVStream()
{
    while(m_bThreadRuning)
    {
        if(m_bConnected == false){
            usleep(100*1000);
            continue;
        }

        CC_MsgHeader msgHeader;
        memset((char*)&msgHeader,0,sizeof(CC_MsgHeader));

        recvSocketData((char*)&msgHeader,sizeof(CC_MsgHeader));
        //先收包头，拿到包头里面的后面跟着的数据长度
        if(strncmp((char*)msgHeader.headerID,"CCTC",sizeof(msgHeader.headerID)) == 0)
        {
            if(msgHeader.msgType == MSGHEADER_TYPE_AVSTREAM)
            {
                if(msgHeader.length > 0){
                    uint8_t* sBuffer = (uint8_t*)malloc(msgHeader.length);
                    recvSocketData((char*)sBuffer,msgHeader.length);
                    
                    // 根据subType判断是视频还是音频
                    if(msgHeader.subType == CONTENT_AVSTREAM_VIDEO)
                    {
                        // 视频数据
                        CCVideoWriter::GetInstance()->WriteVideoData(sBuffer,msgHeader.length);//MP4
                    }
                    else if(msgHeader.subType == CONTENT_AVSTREAM_AUDIO)
                    {
                        // 音频数据
                        CCVideoWriter::GetInstance()->WriteAudioData(sBuffer,msgHeader.length);//MP4
                    }
                    
                    free(sBuffer);
                }
            }
        }
        usleep(1*1000);
    }
}



/**
 * @brief 关闭Socket连接
 * @details 关闭TCP Socket文件描述符，释放网络资源
 *          功能：调用close()系统调用关闭socket，结束网络连接
 */
void CCVideoClient::closeSocketConnection()
{
    printf("close socket...\n");
    close(m_sockfd);
}

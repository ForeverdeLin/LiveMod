/**
 * @file CCStreamServer.cpp
 * @brief TCP流媒体服务器实现
 * @details 使用TCP Socket实现多客户端音视频流传输
 *          知识点：TCP Socket编程、多客户端管理、非阻塞IO
 */

#include "CCStreamServer.h"
#include "ThreadPool.h"

// 静态成员变量初始化：服务器运行标志
bool CCStreamServer::m_bServerRunning=true; 

/**
 * @brief 构造函数
 * @details 初始化TCP服务器，设置监听socket为无效值
 * @param recvPool 接收任务线程池（可选）
 * @param sendPool 发送任务线程池（可选）
 */
CCStreamServer::CCStreamServer(ThreadPool* recvPool, ThreadPool* sendPool)
    : m_listenSockfd(-1)
    , m_pRecvThreadPool(recvPool)
    , m_pSendThreadPool(sendPool)
{
    printf("StreamServer init with thread pools....\n");
}

/**
 * @brief 析构函数
 * @details 清理资源，关闭监听socket
 */
CCStreamServer::~CCStreamServer()
{
    printf("die\n");
    closeListenSockfd();
}

/**
 * @brief 启动TCP服务器
 * @details 知识点：TCP服务器编程流程
 *          1. 创建socket（socket）
 *          2. 设置socket选项（setsockopt：SO_REUSEADDR）
 *          3. 设置为非阻塞模式（fcntl：O_NONBLOCK）
 *          4. 绑定地址和端口（bind）
 *          5. 开始监听（listen）
 *          6. 接受客户端连接（accept循环）
 * @note 此函数会阻塞在accept循环中，直到m_bServerRunning为false
 */
void CCStreamServer::StartTCPServer()
{
    printf("Start StreamServer 2021....\n");

    // 知识点：创建TCP Socket
    // socket - 创建套接字
    // 参数：
    //   - AF_INET: IPv4地址族
    //   - SOCK_STREAM: TCP协议（面向连接的可靠传输）
    //   - 0: 协议类型（TCP自动选择）
    // 返回：socket文件描述符，<0=失败
    m_listenSockfd = socket(AF_INET,SOCK_STREAM,0);
    if(m_listenSockfd < 0){
        closeListenSockfd();
        return;
    }

    printf("Start server listen socket success. sockfd: %d\n", m_listenSockfd);

    // 知识点：设置Socket选项
    // setsockopt - 设置socket选项
    // SO_REUSEADDR: 允许重用本地地址（解决TIME_WAIT状态下的bind失败问题）
    // 作用：服务器重启后可以立即绑定同一端口，无需等待TIME_WAIT结束
    int opt = 1;
    int len = sizeof(int);
    if(setsockopt(m_listenSockfd,SOL_SOCKET,SO_REUSEADDR,&opt,len) < 0){
        printf("setsockopt error...\n");
        closeListenSockfd();
        return;
    }

    // 知识点：设置非阻塞模式
    // fcntl - 文件控制操作
    // F_SETFL: 设置文件状态标志
    // O_NONBLOCK: 非阻塞模式（accept不会阻塞，立即返回）
    // 作用：避免accept阻塞，可以在循环中处理多个客户端
    fcntl(m_listenSockfd,F_SETFL,O_NONBLOCK);

    // 知识点：配置服务器地址结构
    // sockaddr_in - IPv4地址结构
    struct sockaddr_in server_addr;
    bzero(&server_addr,sizeof(struct sockaddr_in));  // 清零结构体

    server_addr.sin_family = AF_INET;                    // 地址族：IPv4
    server_addr.sin_port = htons(LISTEN_PORT);           // 端口号（主机字节序转网络字节序）
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);    // IP地址：INADDR_ANY表示监听所有网络接口

    // 知识点：绑定Socket到地址
    // bind - 将socket绑定到指定的IP地址和端口
    // 参数：
    //   - m_listenSockfd: socket文件描述符
    //   - (struct sockaddr*)&server_addr: 地址结构指针（需要强制转换）
    //   - sizeof(server_addr): 地址结构大小
    // 返回：0=成功，<0=失败
    int ret = bind(m_listenSockfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(ret < 0){
        printf("bind error...\n");
        closeListenSockfd();
        return;
    }

    printf("Start Server Bind success...\n");

    // 知识点：开始监听连接
    // listen - 使socket进入监听状态，等待客户端连接
    // 参数：
    //   - m_listenSockfd: 监听socket
    //   - 10: 最大等待连接队列长度（backlog）
    // 返回：0=成功，-1=失败
    ret = listen(m_listenSockfd,10);
    if(ret == -1){
        printf("listen error...\n");
        closeListenSockfd();
        return;
    }

    printf("Start Server Listen success...\n");


    //signal(SIGINT, serverExitSignalProcess);

    // 知识点：主循环 - 接受客户端连接
    // 由于socket设置为非阻塞，accept会立即返回
    // 如果没有新连接，sockfd < 0，继续循环
    while(m_bServerRunning)
    {
        // 知识点：接受客户端连接
        // accept - 从监听队列中接受一个连接，创建新的socket用于与客户端通信
        // 参数：
        //   - m_listenSockfd: 监听socket
        //   - (struct sockaddr*)&client_addr: 输出参数，客户端地址信息
        //   - &size: 输入输出参数，输入为地址结构大小，输出为实际大小
        // 返回：新的socket文件描述符（用于与客户端通信），<0=失败或无可用的连接
        struct sockaddr_in client_addr;
        bzero(&client_addr,sizeof(client_addr));

        socklen_t size = sizeof(struct sockaddr_in);
        int sockfd = accept(m_listenSockfd,(struct sockaddr*)&client_addr,&size);
        if(sockfd > 0)
        {
            printf("\n============ACCEPT NEW CONNECTION============\n");
            printf("connected socket %d, ip address: %s\n",sockfd,inet_ntoa(client_addr.sin_addr));
            
            // 知识点：为每个客户端创建独立的处理对象
            // CCClientProcess - 客户端处理类，负责接收和发送音视频流
            // 使用线程池管理客户端任务，而不是为每个客户端创建线程
            CCClientProcess *clientProcess = new CCClientProcess();
            clientProcess->StartClientProcessWithSockfd(sockfd, m_pRecvThreadPool, m_pSendThreadPool);  // 使用线程池启动

            // 将客户端添加到管理列表
            m_clients.push_back(clientProcess);
        }
        
        // 检查并清理已断开的客户端
        checkClientRunningStatus();
        
        // 知识点：休眠2秒，避免CPU占用过高
        // usleep - 微秒级休眠（2000*1000微秒 = 2秒）
        // 由于非阻塞accept，如果没有连接会立即返回，需要休眠避免CPU空转
        usleep(2000*1000);
    }

    printf("\nSERVER EXIT\n");

    return;
}

/**
 * @brief 停止服务器执行
 * @details 设置运行标志为false，使主循环退出
 *          知识点：静态成员函数，可以从外部调用停止服务器
 */
void CCStreamServer::StopServerExec()
{
    m_bServerRunning=false;
}

/**
 * @brief 关闭监听socket
 * @details 清理资源，关闭socket文件描述符
 */
void CCStreamServer::closeListenSockfd()
{
    if( m_listenSockfd ){
        close(m_listenSockfd);  // 关闭socket
        m_listenSockfd = -1;
    }
}

/**
 * @brief 检查客户端运行状态
 * @details 遍历所有客户端，移除已断开的客户端
 *          知识点：vector遍历和删除，注意删除时索引的处理
 */
void CCStreamServer::checkClientRunningStatus()
{
    if(m_clients.size() <= 0){
        return;
    }

    // 遍历所有客户端
    for(int i=0; i < m_clients.size(); i++)
    {
        CCClientProcess *clientPtr = (CCClientProcess*)m_clients[i];
        if(clientPtr != NULL)
        {
            // 检查客户端是否还在运行
            bool runStatus = clientPtr->GetClientRunningStatus();
            if(runStatus == false)
            {
                printf("\n run status == false erase... %d",i);
                // 从vector中移除
                m_clients.erase(m_clients.begin()+i);
                // 释放内存
                delete clientPtr;
                // 注意：删除后i不变，下次循环会检查下一个元素（因为元素前移了）
            }
        }
    }
}

/**
 * @brief 向所有客户端发送音视频流
 * @details 知识点：多播发送
 *          将编码后的音视频数据发送给所有连接的客户端
 *          每个客户端有独立的消息队列，实现异步发送
 * @param pBuff 音视频数据缓冲区（H264或AAC编码后的数据）
 * @param len 数据长度（字节）
 * @param type 数据类型（H264视频或AAC音频）
 */
void CCStreamServer::SendAVStream(uint8_t* pBuff, uint32_t len, uint16_t type, int64_t timestamp)
{
    // 先清理已断开的客户端
    checkClientRunningStatus();

    // 遍历所有客户端，将数据加入每个客户端的发送队列
    for(int i =0; i<m_clients.size();i++)
    {
        CCClientProcess * clientPtr=(CCClientProcess*)m_clients[i];
        if(clientPtr!=NULL)
        {
            // 知识点：消息队列
            // EnqueueAVStream - 将数据加入客户端的发送队列
            // 每个客户端有独立的发送线程，从队列中取数据发送
            // 这样可以实现异步发送，不阻塞主线程
            // 传递时间戳，用于音视频同步
            clientPtr->EnqueueAVStream(pBuff,len,type,timestamp);
        }
    }
}

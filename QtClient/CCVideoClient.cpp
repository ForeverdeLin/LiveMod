/**
 * @file CCVideoClient.cpp
 * @brief Qt视频客户端实现
 * @details 从流媒体服务器TCP连接接收H.264/AAC数据，解码后通过回调函数传递给Qt界面显示
 *          知识点：TCP Socket客户端、多线程接收、H.264/AAC解码、Qt回调机制、音视频时间戳同步
 */

#include "CCVideoClient.h"
#include "h264decoder.h"
#include "aacdecoder.h"
#include <cstdlib>  // for abs()
#include <cstring>  // for memcpy()

/**
 * @brief 构造函数
 * @details 初始化Qt视频客户端对象
 *          知识点：C++11 std::mutex自动初始化
 */
CCVideoClient::CCVideoClient()
{
    m_sockfd=-1;  // Socket文件描述符（未连接）
    
    // 注意：Windows上不能使用signal，Qt跨平台应用应避免使用
    // signal(SIGINT,clientExitSignalProcess);
    
    // 知识点：C++11 std::mutex自动初始化
    // std::mutex在构造时自动初始化，无需手动调用init函数
    // pthread_mutex_init(&m_recvMutex,NULL);
    // pthread_mutex_init(&m_sendMutex,NULL);
    
    m_bThreadRuning=true;              // 线程运行标志：运行中
    m_bConnected=false;                // 连接状态：未连接
    m_bKeepAliveThreadStatus=false;    // 心跳线程状态：未运行
    m_bRecvAVThreadStatus=false;       // 接收线程状态：未运行
    m_bSyncThreadStatus=false;         // 同步线程状态：未运行
    m_updateCallback=NULL;             // 视频更新回调函数（NULL=未设置）
    m_updateAudioCallback=NULL;         // 音频更新回调函数（NULL=未设置）
    m_userData=0;                       // 用户数据指针
    
    // 音视频同步初始化
    m_startTime = 0;
    m_audioClock = 0;
    m_videoClock = 0;
    m_bSyncStarted = false;
    
    printf("init\n");
}

CCVideoClient::~CCVideoClient()
{
    printf("\n~CCVideoClient() destroy\n");
    //closeSocketConnection();

}


void CCVideoClient::StopSocketClient()
{
    m_bThreadRuning=false;

    m_updateCallback=NULL;

    printf("close socket...\n");
    close(m_sockfd);
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd > 0)
    {
        close(m_sockfd);
        m_sockfd=-1;
    }

    // 等待所有线程退出
    while(m_bKeepAliveThreadStatus)  //c+/
    {
        usleep(1000*1000);
    }
    while(m_bRecvAVThreadStatus)
    {
        usleep(1000*1000);
    }
    while(m_bSyncThreadStatus)
    {
        usleep(1000*1000);
    }
    
    // 清理队列
    {
        std::lock_guard<std::mutex> lock(m_videoQueueMutex);
        while(!m_videoQueue.empty()) {
            VideoFrame frame = m_videoQueue.front();
            m_videoQueue.pop();
            // 释放YUV数据
            if (frame.yuvFrame.luma.dataBuffer != NULL) {
                free(frame.yuvFrame.luma.dataBuffer);
            }
            if (frame.yuvFrame.chromaB.dataBuffer != NULL) {
                free(frame.yuvFrame.chromaB.dataBuffer);
            }
            if (frame.yuvFrame.chromaR.dataBuffer != NULL) {
                free(frame.yuvFrame.chromaR.dataBuffer);
            }
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(m_audioQueueMutex);
        while(!m_audioQueue.empty()) {
            AudioFrame frame = m_audioQueue.front();
            m_audioQueue.pop();
            if (frame.pcmData != NULL) {
                free(frame.pcmData);
            }
        }
    }

}

void CCVideoClient::StartSocketConnection(CC_NetConnectInfo* netInfo)
{
    printf("Start socket connection IP: %s port: %d\n",netInfo->server_ip,netInfo->port);

    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd < 0) {
        printf("socket error! \n");
        return;
    }

    struct sockaddr_in sockAddrIn;
    memset(&sockAddrIn, 0, sizeof(struct sockaddr_in));

    sockAddrIn.sin_family = AF_INET;
    sockAddrIn.sin_port = htons(netInfo->port);
    sockAddrIn.sin_addr.s_addr = htons(INADDR_ANY);

    if (inet_pton(AF_INET, netInfo->server_ip, &sockAddrIn.sin_addr.s_addr) < 0) {
        printf("inet_pton error!\n");
        return;
    }

    // non block
    int flags = fcntl(m_sockfd, F_GETFL, 0);
    fcntl(m_sockfd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(m_sockfd, (struct sockaddr*)&sockAddrIn, sizeof(struct sockaddr));
    printf("socket connection result: %d\n", ret);

    std::thread connectThread(&CCVideoClient::runWaitConnectionThread,this);
    connectThread.detach();

    std::thread recvAVThread(&CCVideoClient::runRecvAVStreamThread,this);
    recvAVThread.detach();

    std::thread keepAliveThread(&CCVideoClient::sendKeepAlivePacketThread,this);
    keepAliveThread.detach();
    
    // 启动音视频同步线程
    std::thread syncThread(&CCVideoClient::runAVSyncThread,this);
    syncThread.detach();

   // detach_thread_create(NULL, (void*)doWaitConnectionThread, (void*)this);//等待链接成功
   // detach_thread_create(NULL, (void*)startRecvStreamThread, (void*)this);

    //sendKeepAlivePacket();
    return ;
}

/*void CCVideoClient::doWaitConnectionThread(long long userData)
{
    CCVideoClient* clientPtr = (CCVideoClient*)userData;
    if(clientPtr != NULL){
        clientPtr->RunWaitConnection();
    }
}*/

void CCVideoClient::runWaitConnectionThread()
{
    int error=0;
    fd_set r_set,w_set;
    FD_ZERO(&w_set);
    FD_ZERO(&r_set);

    FD_SET(m_sockfd,&r_set);//将套接字 m_sockfd 分别加入到 “可读事件集合 r_set” 和 “可写事件集合 w_set” 中
    FD_SET(m_sockfd,&w_set);
    struct timeval timeout={0,0};

    int retValue=select(m_sockfd+1, &r_set, &w_set, NULL, &timeout);//阻塞（或超时）等待 m_sockfd 套接字的 “可读” 或 “可写” 事件，
    //从而判断非阻塞 connect 的最终结果
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


bool CCVideoClient::recvSocketData(char* pBuff, unsigned int uLength)
{
    /*if(!m_bThreadRuning)
    {
        return false;
    }*/
    signal(SIGPIPE, SIG_IGN);

    m_recvMutex.lock();

    unsigned int recvLen=0;
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
            m_recvMutex.unlock();
            printf("socket recv error ret < 0\n");
            return false;

        }

        if( 0==nRet)
        {
            m_recvMutex.unlock();
            printf("socket recv error ret=0\n");
            return false;
        }
        recvLen += nRet;
        pBuff += nRet;
    }

    m_recvMutex.unlock();
    return true;
}


bool CCVideoClient::sendSocketData(char* pBuff, unsigned int ulength)
{

    if(!m_bThreadRuning)
    {
        return false;
    }
    signal(SIGPIPE, SIG_IGN);//作用是将 SIGPIPE 信号的处理动作设置为 “忽略”

    m_sendMutex.lock();

    unsigned int sendLen=0;
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
            m_sendMutex.unlock();
            printf("Socket send error\n");
            return false;            
        }

        if(0 == nRet)
        {
            m_sendMutex.unlock();
            printf("Socket send error\n");
            return false;
        }
        sendLen += nRet;
        pBuff += nRet;
    }

    printf("SEND LEN: %d %d\n",ulength,sendLen);

    //pthread_mutex_unlock(&m_sendMutex);这是POSIX 标准（可移植操作系统接口）的 C 语言风格 API，属于 Linux、Unix 系统下的原生线程库,
    m_sendMutex.unlock();//这是 C++11 标准引入的标准库线程支持
    return true;
}

/*void CCVideoClient::clientExitSignalProcess(int num)
{
    m_bThreadRuning=false;
    printf("process exit by SIGINT...\n");
}*/


void CCVideoClient::sendKeepAlivePacketThread()
{
  
    while(m_bThreadRuning)
    {
        m_bKeepAliveThreadStatus=true;
        usleep(1000*1000);
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

        bool bRet =sendSocketData((char*)&msgHeader,sizeof(CC_MsgHeader));
        if(!bRet)
        {
            break;
        }
        usleep(1000*1000);
    }
    m_bKeepAliveThreadStatus=false;

    std::cout<<"sendALive thread exit\n"<<std::endl;

}

/*void CCVideoClient::startRecvStreamThread(long long userData)//为什么要多这一步
{
    CCVideoClient* clientPtr = (CCVideoClient*)userData;
    if(clientPtr != NULL){
        clientPtr->RunRecvAVStream();
    }
}*/




void CCVideoClient::runRecvAVStreamThread()
{
    H264Decoder* decoder = new H264Decoder();
    AACDecoder* aacDecoder = new AACDecoder();
    aacDecoder->Initialize(44100, 2); // 初始化AAC解码器：44.1kHz，立体声

    // PCM缓冲区（用于存储解码后的音频数据）
    uint8_t* pcmBuffer = (uint8_t*)malloc(8192); // 足够大的缓冲区
    int pcmBufferSize = 8192;

    while(m_bThreadRuning)
    {
        m_bRecvAVThreadStatus=true;
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
                if(msgHeader.length > 0)
                {
                    uint8_t* sBuffer = (uint8_t*)malloc(msgHeader.length);
                    recvSocketData((char*)sBuffer,msgHeader.length);

                    // 知识点：音视频同步
                    // 根据时间戳进行同步处理：将解码后的数据放入队列，由同步线程统一处理
                    int64_t frameTimestamp = msgHeader.timestamp;
                    
                    // 根据subType判断是视频还是音频
                    if(msgHeader.subType == CONTENT_AVSTREAM_VIDEO)
                    {
                        // 视频数据：解码H.264为YUV
                        YUVData_Frame yuvFrame;
                        memset(&yuvFrame, 0, sizeof(YUVData_Frame));

                        if(decoder->DecodeH264Packet(sBuffer, msgHeader.length, (YUVData_Frame*)&yuvFrame))
                        {
                            // 将视频帧放入队列，由同步线程处理
                            ProcessVideoFrame(&yuvFrame, frameTimestamp);
                        }
                    }
                    else if(msgHeader.subType == CONTENT_AVSTREAM_AUDIO)
                    {
                        // 音频数据：解码AAC为PCM
                        int pcmSize = pcmBufferSize;
                        if(aacDecoder->DecodeAAC(sBuffer, msgHeader.length, pcmBuffer, &pcmSize))
                        {
                            if(pcmSize > 0)
                            {
                                // 将音频帧放入队列，由同步线程处理
                                ProcessAudioFrame(pcmBuffer, pcmSize, frameTimestamp);
                            }
                        }
                    }

                    free(sBuffer);
                }
            }
        }
        usleep(1*1000);
    }

    if(decoder != NULL){
        delete decoder;
    }
    if(aacDecoder != NULL){
        aacDecoder->Uninitialize();
        delete aacDecoder;
    }
    if(pcmBuffer != NULL){
        free(pcmBuffer);
    }
    m_bRecvAVThreadStatus=false;
    std::cout<<"recv thread exit\n"<<std::endl;
}



/*void CCVideoClient::closeSocketConnection()
{
    printf("close socket...\n");
    close(m_sockfd);
}*/

void CCVideoClient::SetupUpdateGUICallback(UpdateVideo2GUI_Callback callback, unsigned long userData)
{
    m_userData = userData;//MainWindow
    m_updateCallback = callback;//MainWindow::updateVideoData
}

void CCVideoClient::SetupUpdateAudioCallback(UpdateAudio2GUI_Callback callback, unsigned long userData)
{
    m_userData = userData;//MainWindow
    m_updateAudioCallback = callback;//MainWindow::updateAudioData
}

// 获取当前时间（毫秒）
int64_t CCVideoClient::GetCurrentTime()
{
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// 处理视频帧：解码后放入队列
void CCVideoClient::ProcessVideoFrame(YUVData_Frame* yuvFrame, int64_t timestamp)
{
    if (yuvFrame == NULL) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    
    // 防止队列过大
    if (m_videoQueue.size() >= MAX_QUEUE_SIZE) {
        // 丢弃最旧的帧
        VideoFrame oldFrame = m_videoQueue.front();
        m_videoQueue.pop();
        // 释放旧帧数据
        if (oldFrame.yuvFrame.luma.dataBuffer != NULL) {
            free(oldFrame.yuvFrame.luma.dataBuffer);
        }
        if (oldFrame.yuvFrame.chromaB.dataBuffer != NULL) {
            free(oldFrame.yuvFrame.chromaB.dataBuffer);
        }
        if (oldFrame.yuvFrame.chromaR.dataBuffer != NULL) {
            free(oldFrame.yuvFrame.chromaR.dataBuffer);
        }
    }
    
    // 创建新的视频帧（需要深拷贝YUV数据）
    VideoFrame videoFrame;
    videoFrame.timestamp = timestamp;
    videoFrame.yuvFrame.width = yuvFrame->width;
    videoFrame.yuvFrame.height = yuvFrame->height;
    
    // 深拷贝YUV数据
    if (yuvFrame->luma.dataBuffer != NULL && yuvFrame->luma.length > 0) {
        videoFrame.yuvFrame.luma.length = yuvFrame->luma.length;
        videoFrame.yuvFrame.luma.dataBuffer = (unsigned char*)malloc(yuvFrame->luma.length);
        memcpy(videoFrame.yuvFrame.luma.dataBuffer, yuvFrame->luma.dataBuffer, yuvFrame->luma.length);
    } else {
        videoFrame.yuvFrame.luma.length = 0;
        videoFrame.yuvFrame.luma.dataBuffer = NULL;
    }
    
    if (yuvFrame->chromaB.dataBuffer != NULL && yuvFrame->chromaB.length > 0) {
        videoFrame.yuvFrame.chromaB.length = yuvFrame->chromaB.length;
        videoFrame.yuvFrame.chromaB.dataBuffer = (unsigned char*)malloc(yuvFrame->chromaB.length);
        memcpy(videoFrame.yuvFrame.chromaB.dataBuffer, yuvFrame->chromaB.dataBuffer, yuvFrame->chromaB.length);
    } else {
        videoFrame.yuvFrame.chromaB.length = 0;
        videoFrame.yuvFrame.chromaB.dataBuffer = NULL;
    }
    
    if (yuvFrame->chromaR.dataBuffer != NULL && yuvFrame->chromaR.length > 0) {
        videoFrame.yuvFrame.chromaR.length = yuvFrame->chromaR.length;
        videoFrame.yuvFrame.chromaR.dataBuffer = (unsigned char*)malloc(yuvFrame->chromaR.length);
        memcpy(videoFrame.yuvFrame.chromaR.dataBuffer, yuvFrame->chromaR.dataBuffer, yuvFrame->chromaR.length);
    } else {
        videoFrame.yuvFrame.chromaR.length = 0;
        videoFrame.yuvFrame.chromaR.dataBuffer = NULL;
    }
    
    videoFrame.yuvFrame.pts = timestamp;
    
    m_videoQueue.push(videoFrame);
}

// 处理音频帧：解码后放入队列
void CCVideoClient::ProcessAudioFrame(uint8_t* pcmData, int pcmSize, int64_t timestamp)
{
    if (pcmData == NULL || pcmSize <= 0) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_audioQueueMutex);
    
    // 防止队列过大
    if (m_audioQueue.size() >= MAX_QUEUE_SIZE) {
        // 丢弃最旧的帧
        AudioFrame oldFrame = m_audioQueue.front();
        m_audioQueue.pop();
        if (oldFrame.pcmData != NULL) {
            free(oldFrame.pcmData);
        }
    }
    
    // 创建新的音频帧（深拷贝PCM数据）
    AudioFrame audioFrame;
    audioFrame.timestamp = timestamp;
    audioFrame.pcmSize = pcmSize;
    audioFrame.pcmData = (uint8_t*)malloc(pcmSize);
    memcpy(audioFrame.pcmData, pcmData, pcmSize);
    
    m_audioQueue.push(audioFrame);
}

// 音视频同步线程
void CCVideoClient::runAVSyncThread()
{
    m_bSyncThreadStatus = true;
    
    while(m_bThreadRuning)
    {
        if(!m_bConnected) {
            usleep(10*1000);
            continue;
        }
        
        SyncAVFrames();
        usleep(5*1000);  // 5ms检查一次
    }
    
    m_bSyncThreadStatus = false;
    printf("AV sync thread exit\n");
}

// 同步音视频帧
void CCVideoClient::SyncAVFrames()
{
    // 如果还没有开始同步，等待第一帧音视频数据
    if (!m_bSyncStarted) {
        std::lock_guard<std::mutex> vLock(m_videoQueueMutex);
        std::lock_guard<std::mutex> aLock(m_audioQueueMutex);
        
        if (!m_videoQueue.empty() && !m_audioQueue.empty()) {
            // 使用第一帧的时间戳作为起始时间
            m_startTime = GetCurrentTime();
            m_audioClock = m_audioQueue.front().timestamp;
            m_videoClock = m_videoQueue.front().timestamp;
            m_bSyncStarted = true;
        } else {
            return;
        }
    }
    
    int64_t currentTime = GetCurrentTime();
    int64_t elapsedTime = currentTime - m_startTime;  // 播放已过去的时间
    
    // 处理音频帧（音频作为主时钟）
    {
        std::lock_guard<std::mutex> lock(m_audioQueueMutex);
        while (!m_audioQueue.empty()) {
            AudioFrame& audioFrame = m_audioQueue.front();
            int64_t audioPlayTime = audioFrame.timestamp - m_audioClock + elapsedTime;
            
            // 如果音频帧应该播放了（允许提前5ms）
            if (audioPlayTime <= elapsedTime + 5) {
                // 播放音频
                if (m_updateAudioCallback != NULL) {
                    m_updateAudioCallback(audioFrame.pcmData, audioFrame.pcmSize, m_userData);
                }
                
                // 更新音频时钟
                m_audioClock = audioFrame.timestamp;
                
                // 释放并移除
                free(audioFrame.pcmData);
                m_audioQueue.pop();
            } else {
                break;  // 还没到播放时间
            }
        }
    }
    
    // 处理视频帧（以音频时钟为基准进行同步）
    {
        std::lock_guard<std::mutex> lock(m_videoQueueMutex);
        while (!m_videoQueue.empty()) {
            VideoFrame& videoFrame = m_videoQueue.front();
            
            // 计算视频应该播放的时间（相对于音频时钟）
            int64_t videoPlayTime = videoFrame.timestamp - m_videoClock + elapsedTime;
            int64_t diff = videoPlayTime - elapsedTime;  // 时间差
            
            // 如果视频帧应该播放了（允许±50ms的误差）
            if (abs(diff) <= SYNC_THRESHOLD) {
                // 渲染视频
                if (m_updateCallback != NULL) {
                    m_updateCallback(&videoFrame.yuvFrame, m_userData);
                }
                
                // 更新视频时钟
                m_videoClock = videoFrame.timestamp;
                
                // 释放并移除
                if (videoFrame.yuvFrame.luma.dataBuffer != NULL) {
                    free(videoFrame.yuvFrame.luma.dataBuffer);
                }
                if (videoFrame.yuvFrame.chromaB.dataBuffer != NULL) {
                    free(videoFrame.yuvFrame.chromaB.dataBuffer);
                }
                if (videoFrame.yuvFrame.chromaR.dataBuffer != NULL) {
                    free(videoFrame.yuvFrame.chromaR.dataBuffer);
                }
                m_videoQueue.pop();
            } else if (diff < -SYNC_THRESHOLD) {
                // 视频帧太旧了，丢弃（视频慢了，丢帧）
                if (videoFrame.yuvFrame.luma.dataBuffer != NULL) {
                    free(videoFrame.yuvFrame.luma.dataBuffer);
                }
                if (videoFrame.yuvFrame.chromaB.dataBuffer != NULL) {
                    free(videoFrame.yuvFrame.chromaB.dataBuffer);
                }
                if (videoFrame.yuvFrame.chromaR.dataBuffer != NULL) {
                    free(videoFrame.yuvFrame.chromaR.dataBuffer);
                }
                m_videoQueue.pop();
            } else {
                // 视频帧还没到播放时间，等待
                break;
            }
        }
    }
}

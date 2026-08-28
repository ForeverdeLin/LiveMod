/**
 * @file CCServerController.cpp
 * @brief 流媒体服务器控制器实现
 * @details 集成视频采集、音频采集、编码、RTMP推流、TCP服务器等所有功能模块
 *          知识点：多线程编程、音视频同步、模块化设计
 */

#include "CCServerController.h"
#include <cstdlib>  // for strtof
#include <algorithm> // for std::remove
#include "CCStreamServerDef.h"
#include <string.h>
#include <sys/time.h>

extern "C" {
#include <libavutil/time.h>
}

// 静态成员变量初始化：服务器运行标志
bool CCServerController::m_bServerRunning=true;

/**
 * @brief 构造函数
 * @details 初始化所有功能模块
 *          知识点：模块初始化顺序
 *          1. 视频采集（摄像头）
 *          2. 音频采集（ALSA）
 *          3. TCP服务器
 *          4. 硬件编码器
 *          5. 美颜滤镜
 *          6. RTMP推流
 *          7. WebSocket控制服务器
 */
CCServerController::CCServerController()
    : m_pCamera(nullptr)              // 摄像头设备（V4L2）
    , m_pTCPServer(nullptr)           // TCP流媒体服务器
    , m_pRTMPSender(nullptr)          // RTMP推流器（FFmpeg）
    , m_pHWEncoder(nullptr)           // 硬件视频编码器（可选）
    , m_pBeautyFilter(nullptr)        // 美颜滤镜（OpenCV）
    , m_pWebSocketServer(nullptr)     // WebSocket控制服务器
    , m_pAudioCapture(nullptr)        // ALSA音频采集
    , m_pAACEncoder(nullptr)          // AAC音频编码器
    , m_bStreaming(false)             // RTMP推流状态标志
    , m_pBeautyBuffer(nullptr)        // 美颜处理缓冲区
    , m_beautyBufferSize(0)           // 美颜缓冲区大小
    , m_pPCMBuffer(nullptr)           // PCM音频数据缓冲区
    , m_pAACBuffer(nullptr)           // AAC编码数据缓冲区
    , m_pcmBufferSize(0)              // PCM缓冲区大小
    , m_aacBufferSize(0)              // AAC缓冲区大小
    , m_audioFrameCount(0)            // 音频帧计数器（用于PTS计算）
    , m_audioStartTime(0)             // 音频开始时间戳（毫秒）
{
    // 初始化默认美颜参数，静态成员函数，不用构造对象，先拿到一个美颜参数结构体✔
    m_beautyParams = BeautyFilter::GetDefaultParams();
    
    // 创建线程池（每个线程池8个工作线程）✔
    m_pRecvThreadPool = new ThreadPool(4);   // 接收任务线程池
    m_pSendThreadPool = new ThreadPool(4);   // 发送任务线程池
    
    initVideoCapture();         // 初始化摄像头（V4L2）✔
    initAudioCapture();         // 初始化音频采集（ALSA）✔
    m_pTCPServer = new CCStreamServer(m_pRecvThreadPool, m_pSendThreadPool);  // 创建TCP服务器（传入线程池）
    
    // 初始化新模块
    initHWEncoder();            // 初始化硬件编码器（可选，用于加速）✔
    initBeautyFilter();         // 初始化美颜滤镜（OpenCV）✔
    initRTMPStreaming();        // 初始化RTMP推流（FFmpeg）✔
    initWebSocketServer();      // 初始化WebSocket控制服务器（libwebsockets）

    // 知识点：信号处理
    // signal - 注册信号处理函数
    // SIGINT: 中断信号（Ctrl+C），用于优雅退出
    signal(SIGINT,serverExitSignalProcess);
}

CCServerController::~CCServerController()
{
    m_bStreaming = false;
    
    if(m_pTCPServer!=NULL)
    {
        delete m_pTCPServer;
        m_pTCPServer = nullptr;
    }
    
    if(m_pRecvThreadPool != nullptr)
    {
        delete m_pRecvThreadPool;
        m_pRecvThreadPool = nullptr;
    }
    
    if(m_pSendThreadPool != nullptr)
    {
        delete m_pSendThreadPool;
        m_pSendThreadPool = nullptr;
    }
    
    if(m_pRTMPSender != nullptr)
    {
        m_pRTMPSender->Close();
        delete m_pRTMPSender;
        m_pRTMPSender = nullptr;
    }
    
    if(m_pHWEncoder != nullptr)
    {
        m_pHWEncoder->Uninitialize();
        delete m_pHWEncoder;
        m_pHWEncoder = nullptr;
    }
    
    if(m_pBeautyFilter != nullptr)
    {
        m_pBeautyFilter->Uninitialize();
        delete m_pBeautyFilter;
        m_pBeautyFilter = nullptr;
    }
    
    if(m_pWebSocketServer != nullptr)
    {
        m_pWebSocketServer->Stop();
        delete m_pWebSocketServer;
        m_pWebSocketServer = nullptr;
    }
    
    if(m_pBeautyBuffer != nullptr)
    {
        free(m_pBeautyBuffer);
        m_pBeautyBuffer = nullptr;
    }
    
    unInitAudioCapture();
    unInitVideoCapture();
}


/**
 * @brief 启动服务器控制器
 * @details 知识点：多线程启动
 *          1. 创建采集线程（视频+音频采集和编码）
 *          2. 启动TCP服务器（主线程，阻塞在accept循环）
 */
void CCServerController::StartServerController()
{
    // 知识点：创建分离线程
    // detach_thread_create - 创建分离线程执行采集任务
    // startCaptureThread - 线程入口函数（静态成员函数）
    // this - 传递当前对象指针，用于在静态函数中调用成员函数
    detach_thread_create(NULL, (void*)startCaptureThread, (void*)this);
    
    // 启动TCP服务器（主线程，会阻塞在accept循环中）
    m_pTCPServer->StartTCPServer();
}

/**
 * @brief 采集线程入口函数（静态成员函数）
 * @details 知识点：静态成员函数作为线程入口
 *          静态函数不能直接访问非静态成员，需要通过参数传递对象指针
 * @param userData 用户数据（CCServerController对象指针）
 */
void CCServerController::startCaptureThread(long long userData)//转成整数地址值，
//然后转成对象指针
{
    CCServerController* capturePtr = (CCServerController*)userData;
    if(capturePtr != NULL){
        capturePtr->RunVideoCapture();  // 调用成员函数执行采集循环
    }
}

/**
 * @brief YUYV422格式转换为YUV420P格式
 * @details 将摄像头采集的YUYV422（YUV422交错格式）转换为YUV420P（YUV420平面格式）
 * 
 * YUV422（YUYV）：UV 水平下采样（每 2 个像素共享 1 个 UV）
YUV420：UV 水平和垂直都下采样（每 4 个像素共享 1 个 UV）
 * 
 不会产生格式外影响：
符合标准：YUV420 本身就是 4:2:0 采样，这是格式定义
解码器处理：播放/解码时会通过插值恢复奇数行的 UV
兼容性：H.264 编码器通常接受 YUV420，这是常见输入格式
可能的影响（在预期范围内）：
轻微质量损失：UV 信息减少约 50%，但人眼对色度不敏感
文件大小：YUV420 比 YUV422 小约 25%，有利于编码和传输
 * 
 * 实现方式：
 * 1. YUYV422格式：每个像素2字节，“交错存储”（Y0 U0 Y1 V0 Y2 U1 Y3 V1...），UV分量水平采样
 * 2. YUV420P格式：“平面存储”，Y/U/V三个平面分离，UV分量水平和垂直都下采样（4:2:0）
 * 3. 转换过程：提取所有Y分量到Y平面；对UV分量进行下采样（只取偶数行的UV）到U/V平面
 * 4. 内存布局：Y平面(宽*高) + U平面(宽*高/4) + V平面(宽*高/4) = 总大小(宽*高*3/2)
 * 
 * @param src 源YUYV422数据缓冲区
 * @param dst 目标YUV420P数据缓冲区（已分配足够空间）
 * @param width 视频宽度
 * @param height 视频高度
 */
static void ConvertYUYV422ToYUV420P(const uint8_t* src, uint8_t* dst, int width, int height)
{
    if (!src || !dst || width <= 0 || height <= 0) {
        return;
    }

    uint8_t* yPlane = dst;
    uint8_t* uPlane = yPlane + width * height;
    uint8_t* vPlane = uPlane + (width * height) / 4;

    for (int row = 0; row < height; ++row) {
        const uint8_t* srcRow = src + row * width * 2;
        uint8_t* yRow = yPlane + row * width;
        for (int col = 0; col < width; col += 2) {
            int pairIndex = col / 2;
            int srcIndex = pairIndex * 4;

            yRow[col] = srcRow[srcIndex];
            yRow[col + 1] = srcRow[srcIndex + 2];

            if ((row & 1) == 0) {
                int uvIndex = (row / 2) * (width / 2) + pairIndex;
                uPlane[uvIndex] = srcRow[srcIndex + 1];
                vPlane[uvIndex] = srcRow[srcIndex + 3];
            }
        }
    }
}

/**
 * @brief 音视频采集主循环
 * @details 在独立线程中持续采集视频和音频，进行编码后发送到TCP客户端和RTMP服务器
 * 
 * 实现方式：
 * 1. 视频采集：通过V4L2从摄像头采集YUYV422格式帧，转换为YUV420P格式
 * 2. 美颜处理：可选地对YUV420P数据进行OpenCV美颜处理（磨皮、亮度、对比度、饱和度调整）
 * 3. 视频编码：优先使用硬件编码器（NVENC/VAAPI/QSV），失败则回退到x264软件编码
 * 4. 音频采集：通过ALSA从麦克风采集PCM数据，使用FAAC编码为AAC格式
 * 5. 时间戳计算：为视频和音频分别计算PTS时间戳（基于帧计数和采样率），用于音视频同步
 * 6. 数据发送：将编码后的H.264和AAC数据分别发送到TCP客户端（向后兼容）和RTMP服务器（推流）
 * 7. 循环控制：每10ms循环一次，通过usleep控制采集频率，直到服务器停止标志为false
 * 
 * 音视频同步机制：使用PTS（Presentation Time Stamp）时间戳，客户端根据时间戳对齐播放
 */
void CCServerController::RunVideoCapture()
{
    int yuv420Size = m_pCamera->width * m_pCamera->height * 3 / 2;  // YUV420格式大小
    int yuyvSize = m_pCamera->width * m_pCamera->height * 2;        // YUYV（YUV422）格式大小
    uint8_t* yuv420Buffer = (uint8_t*)malloc(yuv420Size);
    uint8_t* yuyvBuffer = (uint8_t*)malloc(yuyvSize);//422
    if (!yuv420Buffer || !yuyvBuffer) {
        printf("Failed to allocate video buffers\n");
        free(yuv420Buffer);
        free(yuyvBuffer);
        return;
    }

    int64_t frameCount = 0;      // 视频帧计数器
    int64_t startTime = 0;        // 开始时间戳（毫秒）
    
    // 音频相关初始化
    m_audioStartTime = 0;
    m_audioFrameCount = 0;
    
    // 知识点：主采集循环
    // 循环采集视频和音频，编码后发送
    while(m_bServerRunning)
    {
        // 知识点：采集原始视频帧（YUYV格式）
        // capture_frame_yuyv - 从摄像头读取一帧YUYV422格式的原始数据
        // 返回：0=成功，1=EAGAIN（无数据），-1=失败
        // yuyvBuffer: 输出的YUYV422原始数据缓冲区
        // capturedSize: 实际采集到的数据大小
        // 注意：编码为H264是在后续步骤中进行的（见第307行EncodeFrame）
        size_t capturedSize = 0;
        int captureRet = capture_frame_yuyv(m_pCamera, yuyvBuffer, yuyvSize, &capturedSize);
        if (captureRet != 0 || capturedSize == 0)
        {
            usleep(10*1000);
            continue;
        }

        ConvertYUYV422ToYUV420P(yuyvBuffer, yuv420Buffer, m_pCamera->width, m_pCamera->height);

        // 美颜处理：在编码之前对YUV420P数据进行美颜处理
        uint8_t* encodeInputBuffer = yuv420Buffer;  // 默认使用原始数据
        if (m_pBeautyFilter != nullptr && m_pBeautyBuffer != nullptr)
        {
            // 读取美颜参数（加锁保护，因为可能通过WebSocket动态修改）
            BeautyFilterParams beautyParams;
            {
                std::lock_guard<std::mutex> lock(m_beautyParamsMutex);
                beautyParams = m_beautyParams;
            }
            
            // 应用美颜滤镜
            int beautyOutputSize = m_beautyBufferSize;
            if (m_pBeautyFilter->ApplyFilter(yuv420Buffer, yuv420Size, m_pBeautyBuffer, 
                                            &beautyOutputSize, beautyParams))
            {
                // 美颜成功，使用美颜后的数据
                encodeInputBuffer = m_pBeautyBuffer;
            }
            // 如果美颜失败，继续使用原始数据
        }

        bool encoded = false;
        bool encodedKeyFrame = false;

        if (m_pHWEncoder != nullptr)
        {
/*安全裕度：
YUV420P 原始数据：width * height * 1.5
H264 编码后通常更小，但关键帧（I 帧）或复杂场景可能较大（画面突然丰富）
*3 提供约 2 倍的安全裕度，避免缓冲区溢出
实际用途：outputSize 是输入/输出参数：
输入：表示缓冲区容量上限（用于检查是否足够）
输出：函数会修改它，返回实际编码后的数据大小*/
            int outputSize = m_pCamera->width * m_pCamera->height * 3;
            bool hwKeyFrame = false;
            if (m_pHWEncoder->EncodeFrame(encodeInputBuffer, yuv420Size, 
                m_pCamera->h264_buf, &outputSize, &hwKeyFrame))
            {
                m_pCamera->h264_length = outputSize;
                encoded = true;//转换为264完成标志位
                encodedKeyFrame = hwKeyFrame;
/*编码阶段：通过帧计数器（m_frameCount 或 frameCount）控制，每30帧强制生成一个关键帧
关键帧生成：由编码器根据计数器自动控制，无需在采集时干预*/
            }
        }

        if (!encoded)//不成功就用软件编码
        {
            int encLength = h264_compress_frame(&m_pCamera->encoder, -1, encodeInputBuffer, m_pCamera->h264_buf, true);
            if (encLength > 0)
            {
                m_pCamera->h264_length = encLength;
                encoded = true;
                encodedKeyFrame = (frameCount % m_pCamera->encoder.param->i_keyint_max == 0);
            }
        }

        if (encoded && m_pCamera->h264_length > 0)
        {
            // 知识点：计算视频PTS（Presentation Time Stamp）
            // PTS = 帧数 * 1000 / 帧率（转为毫秒）
            int64_t videoPts = (frameCount * 1000) / m_pCamera->encoder.param->i_fps_num;
            
            // 1. 发送到TCP客户端（向后兼容旧客户端）
            if(m_pTCPServer != NULL)
            {
                m_pTCPServer->SendAVStream(m_pCamera->h264_buf, m_pCamera->h264_length, CONTENT_AVSTREAM_VIDEO, videoPts);
            }
            
            // 2. 如果启用了RTMP推流，发送到RTMP服务器
            if(m_bStreaming && m_pRTMPSender != nullptr)
            {
                m_pRTMPSender->SendH264Frame(m_pCamera->h264_buf, m_pCamera->h264_length, 
                                            encodedKeyFrame, videoPts);
            }
            
            frameCount++;
        }

        // 知识点：音频采集和编码流程
        // 1. 从ALSA设备读取PCM数据
        // 2. 编码为AAC
        // 3. 发送到TCP客户端和RTMP服务器
        if (m_pAudioCapture && m_pAudioCapture->IsCapturing() && m_pAACEncoder)
        {
            // 读取一帧PCM数据
            int pcmSize = m_pAudioCapture->ReadFrame(m_pPCMBuffer, m_pcmBufferSize);
            if (pcmSize > 0)
            {
                // 编码为AAC
                int aacSize = m_aacBufferSize;
                if (m_pAACEncoder->EncodeFrame(m_pPCMBuffer, pcmSize, m_pAACBuffer, &aacSize))
                {
                    if (aacSize > 0)
                    {
                        // 知识点：计算音频PTS
                        // 如果首次，记录开始时间
                        if (m_audioStartTime == 0) {
                            m_audioStartTime = av_gettime() / 1000; // 毫秒
                        }
                        // PTS = 帧数 * 每帧样本数 / 采样率 * 1000（转为毫秒）
                        // 例如：100帧 * 1024样本/帧 / 44100Hz * 1000 = 2322ms
                        int64_t audioPts = (m_audioFrameCount * 1000 * m_pAudioCapture->GetFramesPerPeriod()) 
                                          / m_pAudioCapture->GetSampleRate();
                        
                        // 1. 发送AAC数据到TCP客户端（传递时间戳用于同步）
                        if(m_pTCPServer != NULL)
                        {
                            m_pTCPServer->SendAVStream(m_pAACBuffer, aacSize, CONTENT_AVSTREAM_AUDIO, audioPts);
                        }
                        
                        // 2. 如果启用了RTMP推流，发送到RTMP服务器
                        if(m_bStreaming && m_pRTMPSender != nullptr)
                        {
                            m_pRTMPSender->SendAACFrame(m_pAACBuffer, aacSize, audioPts);
                        }
                        
                        m_audioFrameCount++;
                    }
                }
            }
        }

        // 知识点：控制采集频率
        // usleep(10*1000) = 10毫秒 = 约100fps采集频率
        // 实际帧率由摄像头和编码器决定，这里只是控制循环频率
        usleep(10*1000);
    }
    
    free(yuv420Buffer);
    free(yuyvBuffer);
    
    m_pTCPServer->StopServerExec();
}




/**
 * @brief 初始化视频采集模块
 * @details 完成摄像头设备的初始化、参数配置、采集启动及编码器初始化
 *          主要流程包括：
 *          1. 分配摄像头设备结构体内存
 *          2. 配置摄像头基本参数（设备路径、分辨率、像素格式）
 *          3. 打开并初始化摄像头设备，启动视频采集
 *          4. 初始化H.264编码器并分配编码数据缓冲区
 * 
 * @implementation 实现方式：
 *          - camera_open: 使用stat检查设备文件类型，通过open系统调用打开/dev/video0设备文件
 *          - camera_init: 通过V4L2 ioctl查询设备能力(VIDIOC_QUERYCAP)，设置视频格式(VIDIOC_S_FMT)，
 *            使用内存映射模式(VIDIOC_REQBUFS+mmap)分配采集缓冲区，实现零拷贝高效采集
 *          - camera_capturing_start: 将所有内存映射缓冲区加入采集队列(VIDIOC_QBUF)，
 *            发送VIDIOC_STREAMON命令启动视频流，驱动开始向缓冲区写入视频数据
 *          - h264_encoder_init: 使用x264库初始化编码器，设置参数(分辨率640x360、帧率15fps、
 *            baseline profile、veryfast预设)，调用x264_encoder_open打开编码器并分配图片缓冲区
 *  
 * * @implementation 初始化顺序：
 *          1. 分配摄像头结构体内存并配置基本参数(设备路径、分辨率640x360)
 *          2. camera_open: 打开/dev/video0设备文件，获取文件描述符
 *          3. camera_init: 查询设备能力、设置视频格式、使用mmap分配采集缓冲区
 *          4. camera_capturing_start: 将缓冲区加入队列并启动视频流，开始采集
 *          5. h264_encoder_init: 初始化x264编码器(15fps、baseline profile)作为后备方案
 *          6. 分配H.264编码数据缓冲区，完成初始化
 * @return 无返回值，通过打印信息指示初始化结果
 */
void CCServerController::initVideoCapture()
{
    printf("\nInit video capture...\n");

    // 分配摄像头设备结构体内存（IOTC_Camera类型）
    m_pCamera = (IOTC_Camera *)malloc(sizeof(IOTC_Camera));
    if (!m_pCamera) {
        printf("malloc camera failure!\n");
        return;
    }

    // 配置摄像头基本参数
    m_pCamera->device_name = "/dev/video0";  // V4L2摄像头设备路径
    m_pCamera->buffers = NULL;               // 采集缓冲区（后续由camera_init分配）
    m_pCamera->width = 640;                  // 视频宽度（像素）
    m_pCamera->height = 360;                 // 视频高度（像素）
    m_pCamera->display_depth = 5;            // 像素格式：5对应RGB24格式

    // 摄像头设备操作：打开->初始化->启动采集
    camera_open(m_pCamera);                  // 打开摄像头设备
    camera_init(m_pCamera);                  // 初始化摄像头（分配缓冲区等）
    camera_capturing_start(m_pCamera);       // 开始视频采集， 摄像头数据直接
    //放到这个结构体里的buf数组

    // 初始化H.264编码器（基于x264）
    // 注意：即使实现了硬件编码器自动选择，x264仍然需要初始化，原因：
    // 1. 作为后备方案：硬件编码器可能初始化失败或运行时出错，需要回退到x264软件编码
    // 2. 提供帧率参数：代码在多处使用 m_pCamera->encoder.param->i_fps_num 获取帧率（15fps）
    //    用于计算PTS、初始化硬件编码器和RTMP推流
    h264_encoder_init(&m_pCamera->encoder, m_pCamera->width, m_pCamera->height);
    
    // 分配H.264编码数据缓冲区（大小为宽*高*3，确保足够存储编码后数据）
    m_pCamera->h264_buf = (uint8_t *)malloc(sizeof(uint8_t) * m_pCamera->width * m_pCamera->height * 3);
    printf("\ncapture okkkkkk\n");
}   //设置缓冲区


void CCServerController::serverExitSignalProcess(int num)
{
    printf("\nServer exit signal process...\n");
    m_bServerRunning = false;
}

void CCServerController::unInitVideoCapture()
{
    if(m_pCamera == NULL){
        return ;
    }

    h264_encoder_uninit(&m_pCamera->encoder);
    if(m_pCamera->h264_buf != NULL){
        free(m_pCamera->h264_buf);
    }

    camera_uninit(m_pCamera);
    camera_close(m_pCamera);
    free(m_pCamera);
    m_pCamera = nullptr;
}

/**
 * @brief 初始化音频采集系统
 * @details 实现步骤：
 *          1. 创建ALSA音频采集对象，初始化默认音频设备（44100Hz，立体声，S16格式）
 *          2. 创建AAC编码器，初始化编码参数（44100Hz，2声道，128kbps码率）
 *          3. 根据采集参数计算缓冲区大小，分配PCM原始音频缓冲区和AAC编码缓冲区
 *          4. 启动音频采集，开始从ALSA设备读取音频数据
 * @return 无返回值，通过打印信息指示初始化结果
 */
void CCServerController::initAudioCapture()
{
    printf("\nInit audio capture...\n");
    
    // 创建ALSA音频采集对象
    m_pAudioCapture = new ALSAAudioCapture();
    
    // 初始化音频采集（默认设备，44100Hz，立体声）
    if (!m_pAudioCapture->Initialize("default", 44100, 2, SND_PCM_FORMAT_S16_LE)) {
        printf("Failed to initialize audio capture\n");
        delete m_pAudioCapture;
        m_pAudioCapture = nullptr;
        return;
    }
    
    // 创建AAC编码器
    m_pAACEncoder = new AACEncoder();
    if (!m_pAACEncoder->Initialize(44100, 2, 128000)) {
        printf("Failed to initialize AAC encoder\n");
        delete m_pAACEncoder;
        m_pAACEncoder = nullptr;
        // 音频采集仍然保留，但编码失败
        return;
    }
    
    // 分配PCM和AAC缓冲区
    int framesPerPeriod = m_pAudioCapture->GetFramesPerPeriod();
    int bytesPerSample = 2; // S16格式，2字节/样本
    m_pcmBufferSize = framesPerPeriod * m_pAudioCapture->GetChannels() * bytesPerSample;
    m_pPCMBuffer = (uint8_t*)malloc(m_pcmBufferSize);
    
    // AAC缓冲区大小（通常比PCM小）
    m_aacBufferSize = m_pcmBufferSize; // 足够大
    m_pAACBuffer = (uint8_t*)malloc(m_aacBufferSize);
    
    if (!m_pPCMBuffer || !m_pAACBuffer) {
        printf("Failed to allocate audio buffers\n");
        if (m_pPCMBuffer) {
            free(m_pPCMBuffer);
            m_pPCMBuffer = nullptr;
        }
        if (m_pAACBuffer) {
            free(m_pAACBuffer);
            m_pAACBuffer = nullptr;
        }
        return;
    }
    
    // 开始采集，处于就绪状态
    if (!m_pAudioCapture->StartCapture()) {
        printf("Failed to start audio capture\n");
    } else {
        printf("Audio capture initialized and started successfully\n");
    }
}

void CCServerController::unInitAudioCapture()
{
    if (m_pAudioCapture) {
        m_pAudioCapture->StopCapture();
        m_pAudioCapture->Uninitialize();
        delete m_pAudioCapture;
        m_pAudioCapture = nullptr;
    }
    
    if (m_pAACEncoder) {
        m_pAACEncoder->Uninitialize();
        delete m_pAACEncoder;
        m_pAACEncoder = nullptr;
    }
    
    if (m_pPCMBuffer) {
        free(m_pPCMBuffer);
        m_pPCMBuffer = nullptr;
    }
    
    if (m_pAACBuffer) {
        free(m_pAACBuffer);
        m_pAACBuffer = nullptr;
    }
    
    m_pcmBufferSize = 0;
    m_aacBufferSize = 0;
}

/**
 * @brief 初始化硬件编码器
 * @details 实现方式：首先调用DetectAvailableHWAccel检测系统可用的硬件加速类型，若检测到可用硬件
 *          （且不是NONE），则选择第一个作为硬件类型；然后创建HWVideoEncoder对象并设置编码参数
 *          （分辨率、帧率、码率2Mbps、GOP大小30、预设fast、profile baseline）；接着调用Initialize
 *          初始化编码器；若初始化失败，删除对象并设置m_pHWEncoder为nullptr，系统将回退到软件编码（x264）。
 *          采用"硬件优先，失败回退"策略，确保系统总能正常工作。
 */
void CCServerController::initHWEncoder()
{
    printf("\nInitializing hardware encoder...\n");
    
    // 1.检测硬件加速类型 → 选择硬件类型（VAAPI/NVENC/QSV等）
    auto availableHW = HWVideoEncoder::DetectAvailableHWAccel();
    HWAccelType hwType = HWAccelType::NONE;//最开始表示软件编码
    
    if (!availableHW.empty() && availableHW[0] != HWAccelType::NONE) {
        hwType = availableHW[0];//有可用才换成别的
        printf("Using hardware acceleration: %d\n", (int)hwType);
    } else {
        printf("No hardware acceleration available, using software encoding\n");
    }
    
    // 创建硬件编码器对象
    m_pHWEncoder = new HWVideoEncoder();
    
    EncoderParams params;
    params.width = m_pCamera->width;
    params.height = m_pCamera->height;
    params.fps = m_pCamera->encoder.param->i_fps_num;   
    params.bitrate = 2000000; // 2Mbps
    params.gopSize = 30;
    params.hwType = hwType;
    params.preset = "fast";
    params.profile = "baseline";
    
    if (!m_pHWEncoder->Initialize(params)) {
        printf("Failed to initialize hardware encoder, falling back to x264\n");
        delete m_pHWEncoder;
        m_pHWEncoder = nullptr;
    } else {
        printf("Hardware encoder initialized successfully\n");
    }
}

void CCServerController::initBeautyFilter()
{
    printf("\nInitializing beauty filter...\n");
    
    m_pBeautyFilter = new BeautyFilter();
    
    if (!m_pBeautyFilter->Initialize(m_pCamera->width, m_pCamera->height)) {
        printf("Failed to initialize beauty filter\n");
        delete m_pBeautyFilter;
        m_pBeautyFilter = nullptr;
    } else {
        printf("Beauty filter initialized successfully\n");
        
        // 分配美颜处理缓冲区
        m_beautyBufferSize = m_pCamera->width * m_pCamera->height * 3 / 2;
        m_pBeautyBuffer = (uint8_t*)malloc(m_beautyBufferSize);
    }
}


/*
等待客户端命令再推流


*/
/**
 * @brief 初始化RTMP推流模块
 * @details 创建FFmpegRTMPSender对象但不立即启动推流，设置m_bStreaming为false等待WebSocket命令触发。
 *          采用延迟初始化策略，只有在收到start_stream命令时才真正初始化并开始推流。
 */
void CCServerController::initRTMPStreaming()
{
    printf("\nInitializing RTMP streaming...\n");
    
    // 默认不启动推流，等待WebSocket命令
    m_bStreaming = false;
    
    // 创建RTMP推流器（但不立即初始化）
    m_pRTMPSender = new FFmpegRTMPSender();
    
    printf("RTMP streaming module ready (not started yet)\n");
}



/**
 * @brief 初始化WebSocket控制服务器
 * @details 创建WebSocketControlServer对象，为UPDATE_FILTER、START_STREAM、STOP_STREAM、ADD_RTMP_URL等命令
 *          注册lambda回调函数，所有回调都统一调用handleWebSocketCommand处理。最后启动服务器监听8080端口，
 *          如果启动失败则清理资源。
 */
void CCServerController::initWebSocketServer()
{
    printf("\nInitializing WebSocket control server...\n");
    
    m_pWebSocketServer = new WebSocketControlServer();
    
    // 注册命令回调
    m_pWebSocketServer->RegisterCommandCallback(
        ControlCommand::UPDATE_FILTER,         [this](const std::string& json) {

            this->handleWebSocketCommand(json);
        }
    );
    
    m_pWebSocketServer->RegisterCommandCallback(ControlCommand::START_STREAM,
        [this](const std::string& json) {
            this->handleWebSocketCommand(json);
        });
    
    m_pWebSocketServer->RegisterCommandCallback(ControlCommand::STOP_STREAM,
        [this](const std::string& json) {
            this->handleWebSocketCommand(json);
        });
    
    m_pWebSocketServer->RegisterCommandCallback(ControlCommand::ADD_RTMP_URL,
        [this](const std::string& json) {
            this->handleWebSocketCommand(json);
        });
    
    // 启动WebSocket服务器（端口8080）
    if (!m_pWebSocketServer->Start(8080)) {
        printf("Failed to start WebSocket server\n");
        delete m_pWebSocketServer;
        m_pWebSocketServer = nullptr;
    } else {
        printf("WebSocket control server started on port 8080\n");
    }
}

/**
 * @brief 简单的JSON值提取函数（手写实现，不依赖外部库）
 * @details 通过字符串查找定位key的位置，跳过空格后使用strtof函数解析浮点数值。
 *          如果找不到key或解析失败则返回默认值。实现简单但不支持复杂JSON结构。
 * @param json JSON字符串
 * @param key 要提取的key
 * @param defaultValue 默认值（如果找不到）
 * @return 提取的浮点数值
 *///(paramsSection, "smooth", m_beautyParams.smoothLevel);
static float ExtractFloatFromJSON(const std::string& json, const std::string& key, float defaultValue = 0.0f)
{
    // 查找 "key": 模式
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return defaultValue;
    }
    
    // 跳过key和冒号
    pos += pattern.length();
    
    // 跳过空格
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    
    // 提取数值（可能是负数）
    if (pos >= json.length()) {
        return defaultValue;
    }
    
    // 解析浮点数
    char* endPtr = nullptr;
    float value = strtof(json.c_str() + pos, &endPtr);//endPtr 指向第一个无法解析的字符
    //strtof 解析时会自动跳过起始的空白字符（空格、换行、制表符），比如字符串是"smooth": 0.8,（0前有两个空格）：
    //能解析出有效浮点数（包括 0、负数、小数）→ 返回对应的 float 值；
    if (endPtr == json.c_str() + pos) {
        return defaultValue;  // 解析失败
    }
    
    return value;
}

/**
 * @brief 从JSON中提取字符串值，url相关用到的，rtmp推流地址，和获取美颜数值
 * @details 通过字符串查找定位key和引号的位置，使用substr提取引号之间的字符串内容。
 *          如果找不到key或引号不匹配则返回默认值。
 */
static std::string ExtractStringFromJSON(const std::string& json, const std::string& key, const std::string& defaultValue = "")
{
    std::string pattern = "\"" + key + "\":\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return defaultValue;
    }
    
    pos += pattern.length();
    size_t endPos = json.find("\"", pos);
    if (endPos == std::string::npos) {
        return defaultValue;
    }
    
    return json.substr(pos, endPos - pos);
}

/**
 * @brief 处理WebSocket接收到的控制命令
 * @details 通过字符串查找识别命令类型（update_filter、start_stream、stop_stream、add_rtmp_url、remove_rtmp_url），
 *          然后分别处理：update_filter使用ExtractFloatFromJSON提取美颜参数并更新；start_stream检查RTMP URL列表
 *          后初始化推流器；stop_stream关闭推流器；add/remove_rtmp_url管理RTMP URL列表。所有操作都使用互斥锁保护共享数据。
 */
void CCServerController::handleWebSocketCommand(const std::string& jsonData)
{
    printf("Received WebSocket command: %s\n", jsonData.c_str());
    
    // 解析update_filter命令
    if (jsonData.find("\"action\":\"update_filter\"") != std::string::npos ||
        jsonData.find("'action':'update_filter'") != std::string::npos) {
        
        // 知识点：JSON解析
        // 方案1：手写简单解析（当前实现，不依赖外部库）
        // 方案2：使用jsoncpp库（更健壮，支持复杂JSON）
        
        std::lock_guard<std::mutex> lock(m_beautyParamsMutex);
        
        // 从JSON中提取美颜参数
        // JSON格式：{"action":"update_filter","params":{"smooth":0.5,"bright":0.1,"contrast":0.1,"saturation":0.0}}
        
        // 查找params对象
        size_t paramsPos = jsonData.find("\"params\":");
        if (paramsPos != std::string::npos) {
            // 在params对象中提取各个参数
            std::string paramsSection = jsonData.substr(paramsPos);//从paramsPos下标开始，截取jsonData剩余所有字符，得到paramsSection
            
            m_beautyParams.smoothLevel = ExtractFloatFromJSON(paramsSection, "smooth", m_beautyParams.smoothLevel);
            m_beautyParams.brightLevel = ExtractFloatFromJSON(paramsSection, "bright", m_beautyParams.brightLevel);
            m_beautyParams.contrastLevel = ExtractFloatFromJSON(paramsSection, "contrast", m_beautyParams.contrastLevel);
            m_beautyParams.saturationLevel = ExtractFloatFromJSON(paramsSection, "saturation", m_beautyParams.saturationLevel);
            
            printf("Beauty filter parameters updated: smooth=%.2f, bright=%.2f, contrast=%.2f, saturation=%.2f\n",
                   m_beautyParams.smoothLevel, m_beautyParams.brightLevel, 
                   m_beautyParams.contrastLevel, m_beautyParams.saturationLevel);
        } else {
            printf("Warning: No 'params' found in update_filter command\n");
        }
    }



    // 解析start_stream命令
    else if (jsonData.find("start_stream") != std::string::npos) {
        if (!m_bStreaming && m_pRTMPSender != nullptr) {
            // 初始化RTMP推流
            std::lock_guard<std::mutex> lock(m_rtmpUrlsMutex);
            if (!m_rtmpUrls.empty()) {
                if (m_pRTMPSender->Initialize(m_pCamera->width, m_pCamera->height,
                                              m_pCamera->encoder.param->i_fps_num,
                                              2000000, m_rtmpUrls)) {
                    m_bStreaming = true;
                    printf("RTMP streaming started\n");
                }
            }
        }
    }
    
    
    // 解析stop_stream命令
    else if (jsonData.find("stop_stream") != std::string::npos) {
        if (m_bStreaming && m_pRTMPSender != nullptr) {
            m_pRTMPSender->Close();
            m_bStreaming = false;
            printf("RTMP streaming stopped\n");
        }
    }
    // 解析add_rtmp_url命令
    else if (jsonData.find("\"action\":\"add_rtmp_url\"") != std::string::npos) {
        // 提取URL
        std::string rtmpUrl = ExtractStringFromJSON(jsonData, "url");
        if (!rtmpUrl.empty()) {
            std::lock_guard<std::mutex> lock(m_rtmpUrlsMutex);
            m_rtmpUrls.push_back(rtmpUrl);
            printf("RTMP URL added: %s\n", rtmpUrl.c_str());
        } else {
            printf("Warning: No URL found in add_rtmp_url command\n");
        }
    }
    // 解析remove_rtmp_url命令
    else if (jsonData.find("\"action\":\"remove_rtmp_url\"") != std::string::npos) {
        std::string rtmpUrl = ExtractStringFromJSON(jsonData, "url");
        if (!rtmpUrl.empty()) {
            std::lock_guard<std::mutex> lock(m_rtmpUrlsMutex);
            m_rtmpUrls.erase(    std::remove(m_rtmpUrls.begin(), m_rtmpUrls.end(), rtmpUrl),    m_rtmpUrls.end());！！！
            //std::remove：把要删的元素 “移到末尾”；返回值：指向第一个待删除元素的迭代器（即 “有效元素” 和 “待删元素” 的分界点）
            //erase：真正删除 “待删区间”；作用：删除 [remove返回的迭代器, end) 区间的所有元素（即所有等于 rtmpUrl 的元素）。
            //如果 m_rtmpUrls 是 std::list<std::string>，可直接用成员函数 remove，但 vector 没有成员 remove，必须用 std::remove + erase。
            //迭代器（iterator）可以理解为：“指向容器中元素的‘智能指针’”，
            //但不完全等价于普通指针（比如vector的迭代器可能是指针，list的迭代器是封装的节点指针）。
            //直接 for 循环 + erase 的错误示例（必崩 / 漏删）；迭代器失效导致崩溃）
            //迭代器失效的本质（以 vector 为例）vector的底层是连续的内存数组，erase 一个元素后：
            //被删元素后面的所有元素都会往前移动一位；
            //原迭代器指向的位置要么被覆盖，要么超出有效范围，变成 “野迭代器”（类似野指针）；
            //对野迭代器做++/*操作，会触发未定义行为（崩溃、乱码、程序异常）。
            printf("RTMP URL removed: %s\n", rtmpUrl.c_str());
        }
    }
}






/**
 * @file CCVideoWriter.cpp
 * @brief MP4视频写入器实现
 * @details 使用FFmpeg库将H.264/AAC数据写入MP4文件
 *          知识点：FFmpeg格式API、MP4文件格式、单例模式、H.264 NALU处理
 */

#include "CCVideoWriter.h"

// 静态成员变量初始化
CCVideoWriter* CCVideoWriter::m_pInstance=NULL;  // 单例实例指针
std::mutex CCVideoWriter::m_mutex;              // 互斥锁（用于线程安全的单例创建）
CCVideoWriter::Garbage CCVideoWriter::m_garbage; // 垃圾回收对象（程序退出时自动释放）

static unsigned char avcCBytes[256]={0};  // AVC配置字节（用于MP4元数据）

/**
 * @brief 获取单例实例
 * @details 知识点：单例模式（Double-Checked Locking）
 *          使用双重检查锁定确保线程安全
 *          
 *          单例模式优点：
 *          - 全局唯一实例
 *          - 节省内存
 *          - 便于管理MP4写入资源
 * @return CCVideoWriter单例指针
 */
CCVideoWriter* CCVideoWriter::GetInstance()
{
    // 知识点：双重检查锁定（Double-Checked Locking）
    // 第一次检查：如果实例已存在，直接返回（避免不必要的加锁）
    if(m_pInstance==nullptr)
    {
        // 加锁：防止多线程同时创建实例
        m_mutex.lock();
        // 第二次检查：再次确认实例不存在（因为可能其他线程已经创建）
        if(m_pInstance==nullptr)
        {
            m_pInstance=new CCVideoWriter();  // 创建唯一实例
        }
        m_mutex.unlock();
    }
    return m_pInstance;
}

/**
 * @brief 构造函数
 * @details 初始化MP4写入器的所有成员变量
 *          功能：设置默认视频参数（分辨率、帧率）、初始化状态标志、清空指针
 */
CCVideoWriter::CCVideoWriter()
{
    m_videoWidth    =0;
    m_videoHeight   =0;
    m_nFrameRate    =25;

    m_fileString    =' ';
    m_nFileTotalSize=0;

    m_bRecordStatus=false;

    m_nCurrNALUPos=0;
    m_bFirstIFrame=false;


    m_pVideoStream  =nullptr;
    m_pAudioStream  =nullptr;
    m_pFormatContext =nullptr;
}


/**
 * @brief 启动视频写入器
 * @details 初始化录制状态，准备写入MP4文件
 * @param filePath MP4文件路径
 * @return true=成功，false=失败
 */
bool CCVideoWriter::StartVideoWriterWithPath(std::string filePath)
{
    m_bRecordStatus = true;         // 录制状态：开始
    m_fileString=filePath;          // 保存文件路径
    m_bFirstIFrame=false;           // 首帧I帧标志：未收到
    m_bStartRecordStatus=false;     // 开始录制标志：未开始（等待I帧）

    // 知识点：记录开始时间戳
    // 用于计算相对时间戳（PTS）
    m_startTimeStamp=std::chrono::system_clock::now();
    
    printf("FFmpeg Info: %s\n", avcodec_configuration());
    return true;
}

/**
 * @brief 初始化视频写入器
 * @details 知识点：FFmpeg MP4文件初始化流程
 *          1. 分配格式上下文（avformat_alloc_context）
 *          2. 猜测输出格式（av_guess_format：MP4格式）
 *          3. 初始化视频流（initVideoStreamInfo）
 *          4. 初始化音频流（initAudioStreamInfo）
 *          5. 写入文件头（writeVideoHeader）
 * @return true=成功，false=失败
 */
bool CCVideoWriter::initVideoWriter()
{
    // 知识点：分配格式上下文
    // avformat_alloc_context - 分配AVFormatContext结构
    if(m_pFormatContext == NULL)
    {
        m_pFormatContext = avformat_alloc_context();
        // 知识点：猜测输出格式
        // av_guess_format - 根据格式名称或文件扩展名猜测格式
        // "mov": MP4格式（MP4基于QuickTime MOV格式）
        m_pFormatContext->oformat = av_guess_format("mov", NULL,NULL);
    }

    // 初始化视频流（H.264）
    if(m_pVideoStream == NULL)
    {
        m_pVideoStream = initVideoStreamInfo();
    }

    // 初始化音频流（AAC）
    if(m_pAudioStream == NULL)
    {
        m_pAudioStream = initAudioStreamInfo();
    }

    // 知识点：写入文件头
    // writeVideoHeader - 写入MP4文件头（ftyp、moov等box）
    // 必须在写入数据前调用
    bool status = writeVideoHeader();
    return status;
}

/**
 * @brief 初始化视频流信息
 * @details 知识点：FFmpeg流创建和配置
 *          1. 创建新流（avformat_new_stream）
 *          2. 设置流ID
 *          3. 配置编码器参数（H.264）
 * @return AVStream指针，失败返回NULL
 */
AVStream* CCVideoWriter::initVideoStreamInfo()
{
    // 知识点：创建新流
    // avformat_new_stream - 在格式上下文中创建新的流
    // 参数2：编码器（0=使用外部编码器，不在这里创建）
    AVStream* pOutputStream = avformat_new_stream(m_pFormatContext, 0);
    if (pOutputStream == NULL)
    {
        return NULL;
    }

    // 知识点：设置流ID
    // 流ID必须唯一，通常使用流的索引
    pOutputStream->id = m_pFormatContext->nb_streams - 1;

    // 知识点：配置编码器参数
    // codec - 编码器上下文（旧API，新API使用codecpar）
    AVCodecContext* pCodecContext = pOutputStream->codec;

    pCodecContext->codec_id = AV_CODEC_ID_H264;        // 编码器：H.264
    pCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;    // 媒体类型：视频
    //AVCodecContext* pCodecContext = avcodec_alloc_context3(codec);    新版
    //配置编码器上下文：指定编码器 ID 为 AV_CODEC_ID_H264（即 H.264 编码），
    // 媒体类型为视频（AVMEDIA_TYPE_VIDEO）。

    //设置 H.264 的 AVCC 头部信息：
    //如果存在 SPS（序列参数集）和 PPS（图像参数集）数据，构造 AVCC 格式的头部（这是 H.264 在 MP4/MOV 等容器中存储参数集的标准格式）。
    //将构造好的 AVCC 头部数据设置到编码器上下文的 extradata 中，用于解码器识别视频编码参数。
    if (m_avcCBox.sps_length>0 && m_avcCBox.pps_length>0)
    {
        int spsLen = m_avcCBox.sps_length;
        int ppsLen = m_avcCBox.pps_length;

        unsigned char avccHeader[7] = {0x01, 0x64, 0x00, 0x28, 0xFF, 0xE1, 0x00};
        int hLen = sizeof(avccHeader);
        memcpy(avcCBytes, avccHeader, hLen);

        avcCBytes[hLen] = spsLen;

        memcpy(avcCBytes + hLen + 1, m_avcCBox.spsBuffer, spsLen);
        avcCBytes[spsLen + hLen + 1] = 0x01;
        avcCBytes[spsLen + hLen + 2] = 0x00;
        avcCBytes[spsLen + hLen + 3] = ppsLen;
        memcpy(avcCBytes + spsLen + hLen + 4, m_avcCBox.ppsBuffer, ppsLen);

        int avccLen = spsLen + ppsLen + hLen + 4;

        pCodecContext->extradata = avcCBytes;
        pCodecContext->extradata_size = avccLen;
    }

    //pCodecContext->bit_rate=400000;
    //pCodecContext->time_base.num = 1;
    //pCodecContext->time_base.den = m_nFrameRate;
    //时间基：time_base 用于控制视频帧的时间戳，这里设置为 1/m_nFrameRate（即帧率的倒数）
    pCodecContext->time_base = (AVRational){1,m_nFrameRate};/////////////////////

    //分辨率：设置视频的宽（m_videoWidth）和高（m_videoHeight）
    pCodecContext->width = m_videoWidth;
    pCodecContext->height = m_videoHeight;

    //编码参数：gop_size（ GOP 组大小，影响压缩率和画面质量）、像素格式（AV_PIX_FMT_YUV420P，
    //H.264 常用的 YUV 格式）、max_b_frames（B 帧数量，0 表示不使用 B 帧）。
    pCodecContext->gop_size = 12;
    pCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    pCodecContext->max_b_frames = 0;


    //全局头部标记：如果输出格式要求全局头部（AVFMT_GLOBALHEADER），
    //则设置编码器上下文的对应标记，确保参数集只存储一次。
    if (m_pFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
    {
        pCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    return pOutputStream;
}


/**
 * @brief 初始化音频流信息
 * @details 创建音频流并配置AAC编码参数
 *          功能：创建音频流、设置AAC编码参数（采样率、声道、比特率）、生成AAC配置头
 *          知识点：FFmpeg音频流创建、AAC编码参数配置、extradata设置
 * @return AVStream指针，失败返回NULL
 */
AVStream* CCVideoWriter::initAudioStreamInfo()
{
    //创建音频流：调用 avformat_new_stream 在格式上下文
    // m_pFormatContext 中创建一条新的音频流 pOutputStream。
    AVStream* pOutputStream = avformat_new_stream(m_pFormatContext, 0);
    if (pOutputStream == NULL)
    {
        return NULL;
    }

    //PTS: Presentation Time Stamp, PTS主要用于度量解码后的视频帧什么时候被显示出来
    pOutputStream->id = m_pFormatContext->nb_streams - 1;

    //pOutputStream->pts.val = 0;//这三新版废弃，直接去掉1！！！
    //pOutputStream->pts.num = 1;
    //pOutputStream->pts.den = 1000;
    //帧的时间戳（PTS/DTS）通过 AVPacket.pts 或 AVFrame.pts 单独设置，
    //而非在 AVStream 中统一指定。后续注意

    pOutputStream->time_base.num = 1;
    pOutputStream->time_base.den = 1000;

    pOutputStream->discard = AVDISCARD_NONE;
    //时间基与 PTS 配置：设置流的时间基（time_base）和 PTS（显示时间戳），
    //用于音频帧的时间同步，这里时间基为 1/1000（即毫秒级精度）。

    //编码器上下文配置
    AVCodecContext* pCodecContext = pOutputStream->codec;
    pCodecContext->codec_id = AV_CODEC_ID_AAC;
    pCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
    //编码格式：指定为 AV_CODEC_ID_AAC（AAC 音频编码），媒体类型为音频（AVMEDIA_TYPE_AUDIO）

    //编码参数
    pCodecContext->bit_rate = 8000;//e比特率
    pCodecContext->sample_rate = 8000;//采样率
    pCodecContext->channels = 1;//声道数
    //新版本av_channel_layout_default(&pCodecContext->ch_layout, 1); // 1表示单声道
    pCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;//采样格式：16 位有符号整型
    pCodecContext->profile = FF_PROFILE_AAC_LOW;//AAC 编码 profile新AV_PROFILE_AAC_LOW
    pCodecContext->channel_layout = AV_CH_LAYOUT_MONO;//声道布局：单声道布局
    //新：pCodecContext->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    pCodecContext->block_align = 1;
    pCodecContext->time_base.num = 1;
    pCodecContext->time_base.den = 1000;
    //pCodecContext->frame_size=(int)(g_aacEncodeConfig->inputSamples/(g_aacEncodeConfig->nPCMBits/8));

    unsigned char indexBuffer[2]={0};
    unsigned int sampleIndex=getSampleIndex(8000);
    getIndexConfigure(sampleIndex,1,indexBuffer);//构造 AAC 编码所需的头部索引信息
    //生成两个字节的索引数据，存入 indexBuff 供编码器使用。

    pCodecContext->extradata_size = sizeof(indexBuffer);
    pCodecContext->extradata = indexBuffer;

    if (m_pFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
    {
        pCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    return pOutputStream;
}

/**
 * @brief 获取AAC索引配置
 * @details 根据采样率和声道数生成AAC编码器的extradata配置
 *          功能：构造AAC配置字节，包含对象类型、采样率索引、声道数等信息
 *          知识点：AAC extradata格式、位操作编码
 * @param aSample 采样率索引
 * @param aChannels 声道数
 * @param indexBuff 输出的配置缓冲区（2字节）
 */
void CCVideoWriter::getIndexConfigure(unsigned int aSample,unsigned int aChannels,unsigned char* indexBuff)
{
    unsigned int object_type = 2;

    indexBuff[0] = (object_type<<3) | (aSample>>1);
    indexBuff[1] = ((aSample&1)<<7) | (aChannels<<3);
}

/**
 * @brief 获取采样率索引
 * @details 将采样率值映射为AAC标准定义的采样率索引
 *          功能：根据采样率返回对应的索引值，用于AAC配置
 *          知识点：AAC采样率索引表、标准采样率映射
 * @param aSamples 采样率值（Hz）
 * @return 采样率索引（0-12），未知采样率返回0
 */
int CCVideoWriter::getSampleIndex(unsigned int aSamples)//采样率索引映射
{
    switch(aSamples){
        case 96000: return 0;
        case 88200: return 1;
        case 64000: return 2;
        case 48000: return 3;
        case 44100: return 4;
        case 32000: return 5;
        case 24000: return 6;
        case 22050: return 7;
        case 16000: return 8;
        case 12000: return 9;
        case 11025: return 10;
        case 8000:  return 11;
        case 7350:  return 12;
        default:    return 0;
    }
}

/**
 * @brief 写入视频文件头
 * @details 打开MP4文件并写入文件头（ftyp、moov等box）
 *          功能：打开文件、重置音视频信息、写入MP4文件头（包含所有流信息和编码参数）
 *          知识点：MP4文件格式、avio_open、avformat_write_header
 * @return true=成功，false=失败
 */
bool CCVideoWriter::writeVideoHeader()
{
    //向文件中写入格式头部数据（如文件类型标识、轨道信息、编码参数等）。
    //这是生成多媒体文件的关键步骤 —— 头部信息用于告诉播放器 "这个文件是什么格式、包含哪些音视频流、如何解码"。
    resetAVDataInfomation();

    const char* movieUrl = m_fileString.c_str();
    if (avio_open(&m_pFormatContext->pb, movieUrl, AVIO_FLAG_WRITE) != 0)
    {
        m_bRecordStatus = false;
        printf("Call avio_open function failed.\n");
        return false;
    }

    if (avformat_write_header(m_pFormatContext, NULL) != 0)
    {
        m_bRecordStatus = false;
        printf("Call avformat_write_header function failed.\n");
        return false;
    }

    return true;
}

/**
 * @brief 重置音视频数据信息
 * @details 清空录制统计信息，重置时间戳和文件大小
 *          功能：清零文件总大小、视频信息结构、音频信息结构
 */
void CCVideoWriter::resetAVDataInfomation()
{
    m_nFileTotalSize = 0;
    memset(&m_stFrameInfo, 0, sizeof(m_stFrameInfo));
    memset(&m_stAudioInfo, 0, sizeof(m_stAudioInfo));
}

void CCVideoWriter::StopWriteReleaseResources()
{
    m_bRecordStatus =false;
    printf("STOP WRITE RELEASE RESOURCES:::\n");
    int ret = -1;
    
    if (m_pFormatContext != NULL && m_nFileTotalSize > 0)
    {
        if ((ret = av_write_trailer(m_pFormatContext)) != 0)//////////////
        {
            printf("Call av_write_trailer function failed, return:%d.\n", ret);
        }
    }
    
    if (m_pVideoStream != NULL && m_pVideoStream->codec != NULL)
    {
        if ((ret = avcodec_close(m_pVideoStream->codec)) != 0)
        {
            printf("Call avcodec_close function failed, return:%d.\n", ret);
        }
        m_pVideoStream->codec->extradata=NULL;
        m_pVideoStream = NULL;
    }
    
    if (m_pAudioStream != NULL && m_pAudioStream->codec != NULL)
    {
        if(m_pAudioStream->codec->extradata!=NULL){
            m_pAudioStream->codec->extradata=NULL;
        }
    
        if ((ret = avcodec_close(m_pAudioStream->codec)) != 0)
        {
            printf("Call avcodec_close function failed, return:%d.\n", ret);
        }
        m_pAudioStream = NULL;
    }
    
    if (m_pFormatContext != NULL && m_pFormatContext->pb != NULL && !(m_pFormatContext->oformat->flags & AVFMT_NOFILE))
    {
        if ((ret = avio_close(m_pFormatContext->pb)) != 0)
        {
            printf("Call avio_close function failed, return:%d.\n", ret);
        }
        m_pFormatContext->pb = NULL;
    }
    
    if (m_pFormatContext != NULL)
    {
        avformat_free_context(m_pFormatContext);
        m_pFormatContext = NULL;
    }


    if(g_aacEncodeConfig!=NULL)
    {
        if(g_aacEncodeConfig->pcmBuffer!=NULL)
        {
            free(g_aacEncodeConfig->pcmBuffer);
            g_aacEncodeConfig->pcmBuffer = NULL;
        }

        if(g_aacEncodeConfig->aacBuffer!=NULL)
        {
            free(g_aacEncodeConfig->aacBuffer);
            g_aacEncodeConfig->aacBuffer = NULL;
        }

        if(g_aacEncodeConfig->hEncoder!=NULL)
        {
            faacEncClose(g_aacEncodeConfig->hEncoder);
            g_aacEncodeConfig->hEncoder = NULL;
        }

        free(g_aacEncodeConfig);
        g_aacEncodeConfig = NULL;
    }
    resetAVDataInfomation();
}

/**
 * @brief 从缓冲区读取一个NALU单元
 * @details 解析H.264 Annex-B格式数据，提取一个完整的NALU单元
 *          功能：查找起始码（0x00 0x00 0x00 0x01或0x00 0x00 0x01）、提取NALU类型和数据
 *          知识点：H.264 Annex-B格式、起始码解析、NALU类型提取
 * @param pH264Buff H.264数据缓冲区
 * @param nBufferSize 缓冲区大小
 * @param nOffset 起始偏移量
 * @param nalu 输出的NALU单元结构
 * @return true=成功提取NALU，false=未找到或数据不完整
 */
// 0 0 0 1 67 42 11 25 22 33 0 0 0 1 68 77 55 22 0 0 1 65 41 48 65
bool CCVideoWriter::readOneNaluFromBuff(unsigned char* pH264Buff, unsigned int nBufferSize, unsigned int nOffset, H264_NaluUnit* nalu)
{//遮个和下一个函数都是解析264的
    int i = nOffset;
    while( i < nBufferSize )
    {
        if(pH264Buff[i++] == 0x00 && pH264Buff[i++] == 0x00)
        {
            unsigned char c = pH264Buff[i++];
            if((c == 0x01) || ((c == 0x00) && (pH264Buff[i++] == 0x01)) )
            {
                int pos = i;
                int num = 4;
                while( pos < nBufferSize)
                {
                    if(pH264Buff[pos++] == 0x00 && pH264Buff[pos++] == 0x00)
                    {
                        c = pH264Buff[pos++];
                        if(c == 0x01)
                        {
                            num = 3;
                            break;
                        }
                        else if( (c == 0) && (pH264Buff[pos++] == 0x01) )
                        {
                            num = 4;
                            break;
                        }
                    }
                }
                if(pos == nBufferSize)
                {
                    nalu->size = pos-i;
                }
                else
                {
                    nalu->size = (pos-num)-i;
                }
                nalu->type = pH264Buff[i]&0x1F;//设定类型
                nalu->data = &pH264Buff[i];
                m_nCurrNALUPos = pos-num;
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief 写入视频数据
 * @details 解析H.264 Annex-B格式数据，提取SPS/PPS/IDR/P帧并写入MP4
 *          功能：解析NALU单元、保存SPS/PPS、转换Annex-B为AVCC格式、写入视频帧
 *          知识点：H.264 NALU解析、Annex-B到AVCC转换、MP4视频写入
 * @param pBuff H.264数据缓冲区（Annex-B格式）
 * @param aSize 数据大小
 */
void CCVideoWriter::WriteVideoData(unsigned char* pBuff, int aSize)
{
    if (!m_bRecordStatus)
    {
        return;
    }
    if(aSize < 4){
        return;
    }

    H264_NaluUnit naluUnit;
    memset(&naluUnit, 0, sizeof(naluUnit));

    m_nCurrNALUPos = 0;

    memset(&m_avcCBox, 0, sizeof(m_avcCBox));
    memset(m_avcCBox.ppsBuffer, 0, sizeof(m_avcCBox.ppsBuffer));
    memset(m_avcCBox.spsBuffer, 0, sizeof(m_avcCBox.spsBuffer));

    while(readOneNaluFromBuff(pBuff, aSize, m_nCurrNALUPos, &naluUnit)){
        if(naluUnit.type == H264NT_SPS)//sps
        {
            printf("NALU: SPS:\n");
            if(naluUnit.size > 0)
            {
                memcpy(m_avcCBox.spsBuffer,naluUnit.data,naluUnit.size);
                m_avcCBox.sps_length = naluUnit.size;
            }
            m_bFirstIFrame = true;
        }
        else if(naluUnit.type == H264NT_PPS)//pps
        {
            printf("NALU: PPS:\n");
            if(naluUnit.size > 0)
            {
                memcpy(m_avcCBox.ppsBuffer,naluUnit.data,naluUnit.size);
                m_avcCBox.pps_length = naluUnit.size;
            }
        }
        else if(naluUnit.type == H264NT_SLICE_IDR)//avcc,不是annex-B
        {
            printf("NALU: IDR:\n");//这里给4个字节表示帧的长度
            H264_FrameData* pFrameData = (H264_FrameData*)malloc(sizeof(H264_FrameData));
            memset(pFrameData,0,sizeof(H264_FrameData));

            int datalen = naluUnit.size + 4;
            unsigned char* pData = (unsigned char*)malloc(datalen * sizeof(datalen));
            pData[0] = naluUnit.size >> 24;
            pData[1] = naluUnit.size >> 16;
            pData[2] = naluUnit.size >> 8;
            pData[3] = naluUnit.size & 0xFF;

            memcpy(pData+4, naluUnit.data, naluUnit.size);

            pFrameData->m_nSize = datalen;
            memcpy(pFrameData->m_pDataBuff, pData, datalen);

            pFrameData->m_nCodecID = AV_CODEC_ID_H264;
            pFrameData->m_nTimeStamp = elapseMS(m_startTimeStamp); //时间戳

            //写I帧到ffmpeg
            writeVideoFrame(pFrameData,1);

            free(pData);
            free(pFrameData);
        }
        else if(naluUnit.type == H264NT_SLICE)
        {
            if(m_bFirstIFrame == false)
            {
                printf("NO PPS RETURN!\n");
                m_startTimeStamp=std::chrono::system_clock::now();
                continue;
            }
           H264_FrameData* pFrameData = (H264_FrameData*)malloc(sizeof(H264_FrameData));
            memset(pFrameData,0,sizeof(H264_FrameData));

            int datalen = naluUnit.size + 4;
            unsigned char* pData = (unsigned char*)malloc(datalen * sizeof(unsigned char));
            pData[0] = naluUnit.size >> 24;
            pData[1] = naluUnit.size >> 16;
            pData[2] = naluUnit.size >> 8;
            pData[3] = naluUnit.size & 0xFF;

            memcpy(pData+4, naluUnit.data, naluUnit.size);

            pFrameData->m_nSize = datalen;
            memcpy(pFrameData->m_pDataBuff, pData, datalen);

            pFrameData->m_nCodecID = AV_CODEC_ID_H264;
            pFrameData->m_nTimeStamp = elapseMS(m_startTimeStamp); //时间戳

            //写帧到ffmpeg
            writeVideoFrame(pFrameData,0);

            free(pData);
            free(pFrameData);           
        }
    }
}

/**
 * @brief 设置视频尺寸
 * @details 设置MP4视频的分辨率（宽度和高度）
 *          功能：保存视频宽度和高度，用于初始化视频流参数
 * @param width 视频宽度（像素）
 * @param height 视频高度（像素）
 */
void CCVideoWriter::SetVideoSize(int width, int height)
{
    m_videoWidth = width;
    m_videoHeight=height;
}


/**
 * @brief 写入视频帧
 * @details 将H.264帧数据封装为AVPacket并写入MP4文件
 *          功能：初始化写入器（首次调用）、创建AVPacket、计算时间戳、写入帧数据
 *          知识点：AVPacket封装、PTS/DTS计算、av_interleaved_write_frame、时间基转换
 * @param pData H.264帧数据（AVCC格式）
 * @param keyFlags 是否为关键帧（1=关键帧，0=普通帧）
 * @return true=成功，false=失败
 */
bool CCVideoWriter::writeVideoFrame(H264_FrameData* pData, int keyFlags)
{
    if (!m_bRecordStatus)
    {
        return false;
    }

    if(!initVideoWriter())
    {
        return false;
    }

    m_bStartRecordStatus = true;//录视频后才录音频
    //printf("wrfrc video st------------------->>>\n");

    AVPacket packet;
    av_init_packet(&packet);

    AVRational time_base;
    time_base.num = 1;
    time_base.den = 1000;//和pts什么关系

    if (m_stFrameInfo.m_nLastTimeStamp == 0)//记录时间戳
    {
        m_stFrameInfo.m_nLastTimeStamp = pData->m_nTimeStamp;
    }

    packet.stream_index = m_pVideoStream->index;
    packet.duration = pData->m_nTimeStamp - m_stFrameInfo.m_nLastTimeStamp;

    if(packet.duration<=0){
        printf("vudio duration<=0: %d\n",packet.duration);
        packet.duration=1;
    }

    m_stFrameInfo.m_nTotalTime += packet.duration;
    m_stFrameInfo.m_nLastTimeStamp = pData->m_nTimeStamp;

    int curPts = av_rescale_q(m_stFrameInfo.m_nTotalTime, time_base, m_pVideoStream->time_base);//和pts什么关系
    if (m_stFrameInfo.m_nCurPts >= curPts && curPts != 0)
    {
        return true;
    }

    printf("\n PTS::::: %ld %ld %ld\n",m_stFrameInfo.m_nTotalTime,curPts,packet.duration);
    m_stFrameInfo.m_nCurPts = curPts;

    m_stFrameInfo.m_nCurTime = ((double)m_pVideoStream->pts.val * m_pVideoStream->time_base.num) / m_pVideoStream->time_base.den;

    packet.size = pData->m_nSize;

    packet.data = (uint8_t *)pData->m_pDataBuff;

    packet.dts = packet.pts = m_stFrameInfo.m_nCurPts;
    packet.flags |= (keyFlags>0)?AV_PKT_FLAG_KEY:0;

    m_vmMutex.lock();
        //[videolock lock];mac的
    int ret = av_interleaved_write_frame(m_pFormatContext, &packet);
    m_vmMutex.unlock();
        // [videolock unlock];
    if (ret < 0)
    {
        printf("Call av_write_frame function failed, codecId:%d, size:%d, dts:%lld, duration:%d, return:%d.",
                       pData->m_nCodecID, packet.size, packet.dts, packet.duration, ret);
        av_free_packet(&packet);
        return false;
    }
        
    

    m_nFileTotalSize += packet.size;
    av_free_packet(&packet);

    return true;
}






/**
 * @brief 计算经过的毫秒数
 * @details 计算从指定时间点到现在的毫秒数，用于生成相对时间戳
 *          功能：使用C++11 chrono库计算时间差，返回毫秒数
 *          知识点：C++11 chrono、时间戳计算、相对时间
 * @param startTime 起始时间点
 * @return 经过的毫秒数
 */
uint64_t CCVideoWriter::elapseMS(std::chrono::system_clock::time_point &startTime)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>
           (std::chrono::system_clock::now() - startTime).count();
}

/**
 * @brief 初始化音频编码配置
 * @details 初始化FAAC编码器，配置AAC编码参数并分配缓冲区
 *          功能：打开FAAC编码器、配置编码参数（采样率、声道、比特率）、分配PCM和AAC缓冲区
 *          知识点：FAAC编码器、AAC编码配置、缓冲区分配
 * @return AACEncodeConfig指针，失败返回NULL
 */
AACEncodeConfig* CCVideoWriter::initAudioEncodeConfiguration()
{
    AACEncodeConfig* aacConfig = NULL;
    faacEncConfigurationPtr pConfiguration;

    int nRet = 0;
    int pcmBufferSize = 0;

    aacConfig = (AACEncodeConfig*)malloc(sizeof(AACEncodeConfig));

    aacConfig->nSampleRate = 8000;
    aacConfig->nChannels = 1;
    aacConfig->nPCMBitSize = 16;
    aacConfig->nInputSamples = 0;
    aacConfig->nMaxOutputBytes = 0;

    aacConfig->hEncoder = faacEncOpen(aacConfig->nSampleRate, aacConfig->nChannels, (unsigned long *)&aacConfig->nInputSamples, (unsigned long *)&aacConfig->nMaxOutputBytes);
    if(aacConfig->hEncoder == NULL)
    {
        printf("failed to call faacEncOpen()\n");
        return NULL;
    }

    pcmBufferSize = (int)(aacConfig->nInputSamples*aacConfig->nPCMBitSize/8);

    aacConfig->pcmBuffer=(unsigned char*)malloc(pcmBufferSize*sizeof(unsigned char));
    memset(aacConfig->pcmBuffer, 0, pcmBufferSize);

    aacConfig->aacBuffer=(unsigned char*)malloc(aacConfig->nMaxOutputBytes*sizeof(unsigned char));
    memset(aacConfig->aacBuffer, 0, aacConfig->nMaxOutputBytes);

    pConfiguration = faacEncGetCurrentConfiguration(aacConfig->hEncoder);
    pConfiguration->inputFormat = FAAC_INPUT_16BIT;
    pConfiguration->outputFormat = 0;
    pConfiguration->aacObjectType = LOW;

    nRet = faacEncSetConfiguration(aacConfig->hEncoder, pConfiguration);
    return aacConfig;
}

/**
 * @brief 写入音频流
 * @details 将AAC音频帧数据封装为AVPacket并写入MP4文件
 *          功能：创建AVPacket、计算时间戳、写入音频帧数据
 *          知识点：AVPacket封装、音频PTS计算、av_interleaved_write_frame
 * @param pData AAC音频帧数据
 * @return true=成功，false=失败
 */
bool CCVideoWriter::writeAudioStream(Audio_FrameData* pData)
{
    if (!m_bRecordStatus)
    {
        return false;
    }

    if (m_pAudioStream == NULL || pData == NULL || pData->m_nSize == 0)
    {
        return false;
    }

    //printf("write audio st------------------->>>\n");
    AVPacket packet;
    av_init_packet(&packet);

    AVRational time_base;
    time_base.num = 1;
    time_base.den = 1000;

    if (m_stAudioInfo.m_nLastTimestamp == 0)
    {
        m_stAudioInfo.m_nLastTimestamp = pData->m_nTimeStamp;
    }

    packet.stream_index = m_pAudioStream->index;
    packet.duration = pData->m_nTimeStamp - m_stAudioInfo.m_nLastTimestamp;

    if(packet.duration<=0){
        printf("audio duration<=0: %d\n",packet.duration);
        packet.duration=1;
    }

    m_stAudioInfo.m_nTotalTime += packet.duration;
    m_stAudioInfo.m_nLastTimestamp = pData->m_nTimeStamp;

    int curPts = av_rescale_q(m_stAudioInfo.m_nTotalTime, time_base, m_pAudioStream->time_base);
    if (m_stAudioInfo.m_nCurPts >= curPts && curPts != 0)
    {
        return true;
    }

    m_stAudioInfo.m_nCurPts = curPts;
    m_stAudioInfo.m_nCurTime = ((double)m_pAudioStream->pts.val * m_pAudioStream->time_base.num) / m_pAudioStream->time_base.den;

    packet.size = pData->m_nSize;
    packet.data = (uint8_t *)pData->m_pDataBuff;

    packet.dts = packet.pts = m_stAudioInfo.m_nCurPts;

    m_vMutex.lock();
    int ret = av_interleaved_write_frame(m_pFormatContext, &packet);
    m_vMutex.unlock();

    if (ret < 0)
    {
        printf("Call av_write_frame function failed, codecId:%d, size:%d, dts:%lld, duration:%d, return:%d.\n",
               pData->m_nCodecID, packet.size, packet.dts, packet.duration, ret);
        av_free_packet(&packet);
        return false;
    }

    m_nFileTotalSize += packet.size;
    av_free_packet(&packet);

    return true;
}

/**
 * @brief 线性PCM转AAC
 * @details 将PCM音频数据编码为AAC格式并写入MP4文件
 *          功能：累积PCM数据、调用FAAC编码、将编码后的AAC数据写入MP4
 *          知识点：PCM到AAC编码、FAAC编码器使用、音频数据缓冲
 * @param pData PCM音频数据
 * @param aSize PCM数据大小
 * @return 编码后的AAC数据大小，失败返回-1
 */
int CCVideoWriter::LinearPCM2AAC(unsigned char * pData, int aSize)
{
    int aacBuffSize = (int)(g_aacEncodeConfig->nInputSamples*(g_aacEncodeConfig->nPCMBitSize/8));

    if(pData==NULL){
        return -1;
    }

    if((aSize>aacBuffSize)||(aSize<=0)){
        return -1;
    }

    int nRet = 0;
    int copyLength = 0;

    if(g_bufferRemainSize > aSize){
        copyLength = aSize;
    }
    else{
        copyLength = g_bufferRemainSize;
    }

    memcpy((&g_aacEncodeConfig->pcmBuffer[0]) + g_writeRemainSize, pData, copyLength);
    g_bufferRemainSize -= copyLength;
    g_writeRemainSize += copyLength;

    if(g_bufferRemainSize > 0){
        return 0;
    }

    nRet = faacEncEncode(g_aacEncodeConfig->hEncoder,(int*)(g_aacEncodeConfig->pcmBuffer),g_aacEncodeConfig->nInputSamples,g_aacEncodeConfig->aacBuffer,g_aacEncodeConfig->nMaxOutputBytes);
    memset(g_aacEncodeConfig->pcmBuffer, 0, aacBuffSize);
    g_writeRemainSize = 0;
    g_bufferRemainSize = aacBuffSize;

    Audio_FrameData *pAudioData=(Audio_FrameData*)malloc(sizeof(Audio_FrameData));
    memset(pAudioData, 0, sizeof(Audio_FrameData));

    pAudioData->m_nSize = nRet;
    memcpy(pAudioData->m_pDataBuff, g_aacEncodeConfig->aacBuffer, nRet);

    pAudioData->m_nCodecID = CODEC_ID_AAC;
    pAudioData->m_nTimeStamp = elapseMS(m_startTimeStamp);

    writeAudioStream(pAudioData);

    free(pAudioData);

    memset(g_aacEncodeConfig->pcmBuffer, 0, aacBuffSize);
    if((aSize - copyLength) > 0){
        memcpy((&g_aacEncodeConfig->pcmBuffer[0]), pData+copyLength, aSize - copyLength);
        g_writeRemainSize = aSize - copyLength;
        g_bufferRemainSize = aacBuffSize - (aSize - copyLength);
    }

    return nRet;
}
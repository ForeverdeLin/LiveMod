/**
 * @file RTMP.cpp
 * @brief RTMP推流实现（使用librtmp库）
 * @details 使用librtmp库实现RTMP协议推流
 *          知识点：RTMP协议、AMF编码、NALU解析、librtmp API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "aacstream.h"
#include "h264stream.h"
#include "RTMP.h"

#define SERVER "rtmp://192.168.118.14/test/live"

// 全局变量
RTMP* g_rtmp;              // RTMP连接句柄
bool g_quit = FALSE;        // 退出标志
uint32_t g_startTime;       // 开始时间戳

/**
 * @brief 信号处理函数
 * @details 知识点：信号处理
 *          用于优雅退出RTMP推流
 * @param signo 信号编号
 */
void handler(int signo)
{
    printf("rtmp publish -------------------------exit-------------------\n");
    g_quit = TRUE;
}

/**
 * @brief 初始化RTMP流
 * @details 知识点：librtmp初始化
 *          1. 分配RTMP结构（RTMP_Alloc）
 *          2. 初始化RTMP结构（RTMP_Init）
 *          3. 设置超时时间
 *          4. 设置直播标志（RTMP_LF_LIVE）
 */
void rtmpStreamInit(void)
{
    // 知识点：分配RTMP结构
    // RTMP_Alloc - 分配RTMP连接结构体
    g_rtmp = RTMP_Alloc();
    
    // 知识点：初始化RTMP结构
    // RTMP_Init - 初始化RTMP结构为默认值
    RTMP_Init(g_rtmp);
    
    g_rtmp->Link.timeout = 10;              // 连接超时时间（秒）
    g_rtmp->Link.lFlags |= RTMP_LF_LIVE;    // 设置直播标志（不缓冲）
}

/**
 * @brief 反初始化RTMP流
 * @details 释放RTMP资源
 */
void rtmpStreamDeInit(void)
{
    if(g_rtmp)
    {
        RTMP_Close(g_rtmp);  // 关闭RTMP连接
        RTMP_Free(g_rtmp);   // 释放RTMP结构
        g_rtmp = NULL;
    }
}

/**
 * @brief 连接到RTMP服务器
 * @details 知识点：RTMP连接流程
 *          1. 设置RTMP URL（RTMP_SetURL）
 *          2. 启用写模式（RTMP_EnableWrite：推流模式）
 *          3. 连接到服务器（RTMP_Connect）
 *          4. 连接流（RTMP_ConnectStream）
 * @param url RTMP服务器URL（如：rtmp://server/app/stream）
 * @return true=成功，false=失败
 */
bool rtmpConnectUrl(const char* url)
{
    int ret = 0;
    
    // 知识点：设置RTMP URL
    // RTMP_SetURL - 解析并设置RTMP服务器URL
    // URL格式：rtmp://host:port/app/stream
    ret = RTMP_SetURL(g_rtmp, (char*)url);
    if(ret < 0)
    {
        return FALSE;
    }
    
    // 知识点：启用写模式（推流）
    // RTMP_EnableWrite - 设置为推流模式（发送数据）
    // 不调用此函数则为拉流模式（接收数据）
    RTMP_EnableWrite(g_rtmp);
    
    // 知识点：连接到RTMP服务器
    // RTMP_Connect - 建立TCP连接并完成RTMP握手
    // 返回：>0=成功，<=0=失败
    ret = RTMP_Connect(g_rtmp, NULL);
    if(ret <= 0)
    {
        return FALSE;
    }
    
    // 知识点：连接RTMP流
    // RTMP_ConnectStream - 连接到指定的流（app/stream）
    // 参数2：流ID（0=使用URL中的流名）
    ret = RTMP_ConnectStream(g_rtmp,0);
    if(ret <= 0)
    {
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief 发送元数据（Metadata）
 * @details 知识点：RTMP元数据包、AMF编码
 *          RTMP元数据包含视频的基本信息（宽高、帧率、编码器等）
 *          使用AMF（Action Message Format）编码
 * @param lpMetaData 元数据结构指针（包含宽高、帧率等信息）
 * @return true=成功，false=失败
 */
bool SendMetaData(LPRTMPMetadata lpMetaData)
{
    if(lpMetaData == NULL)
    {
        return FALSE;
    }
    
    // 知识点：创建RTMP数据包
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);      // 重置数据包
    
    RTMPPacket_Alloc(&packet,1024); // 分配1024字节缓冲区
    
    // 知识点：设置数据包头部信息
    packet.m_nChannel = 0x04;                    // 通道ID（控制通道）
    packet.m_packetType = RTMP_PACKET_TYPE_INFO;  // 包类型：信息包（元数据）
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE; // 头部类型：大包
    packet.m_nTimeStamp = 0;                      // 时间戳：0（元数据无时间戳）
    packet.m_nInfoField2 = g_rtmp->m_stream_id;   // 流ID
    packet.m_hasAbsTimestamp = 0;                 // 不使用绝对时间戳
    
    // 知识点：AMF编码
    // AMF（Action Message Format）：RTMP使用的数据编码格式
    // 用于编码元数据、命令等
    char * p = (char *)packet.m_body;
    char * pend = p + 1024;
    
    // 编码字符串："@setDataFrame"（RTMP元数据标识）
    static const AVal av_setDataFrame = AVC("@setDataFrame");
    p = AMF_EncodeString(p, pend, &av_setDataFrame);
    
    // 编码字符串："onMetaData"（元数据事件名）
    static const AVal av_onMetaData = AVC("onMetaData");
    p = AMF_EncodeString(p, pend, &av_onMetaData);
    
    // 知识点：编码ECMA数组（键值对数组）
    *p++ = AMF_ECMA_ARRAY;           // 数组类型标识
    p = AMF_EncodeInt32(p, pend, 5); // 数组元素数量：5个
    
    // 编码元数据字段：duration（时长）
    static const AVal av_duration = AVC("duration");
    p = AMF_EncodeNamedNumber(p, pend, &av_duration, 0);
    
    // 编码元数据字段：width（宽度）
    static const AVal av_width = AVC("width");
    p = AMF_EncodeNamedNumber(p, pend, &av_width, lpMetaData->nWidth);
    
    // 编码元数据字段：height（高度）
    static const AVal av_height = AVC("height");
    p = AMF_EncodeNamedNumber(p, pend, &av_height, lpMetaData->nHeight);
    
    // 编码元数据字段：framerate（帧率）
    static const AVal av_framerate = AVC("framerate");
    p = AMF_EncodeNamedNumber(p, pend, &av_framerate, lpMetaData->nFrameRate);
    
    // 编码元数据字段：videocodecid（视频编码器ID）
    // 7 = H264编码器
    static const AVal av_videocodecid = AVC("videocodecid");
    p = AMF_EncodeNamedNumber(p, pend, &av_videocodecid, 7);
    
    // 知识点：结束数组
    *p++ = 0;              // 数组结束标记
    *p++ = AMF_OBJECT_END; // 对象结束标记
    
    // 设置数据包大小
    packet.m_nBodySize = p - packet.m_body;
    
    // 知识点：发送RTMP数据包
    // RTMP_SendPacket - 发送数据包到RTMP服务器
    RTMP_SendPacket(g_rtmp,&packet,0);
    
    // 释放数据包
    RTMPPacket_Free(&packet);
    
    return TRUE;
}

// 全局变量：H264数据缓冲区
unsigned char m_h264Buf;      // H264数据缓冲区
unsigned int m_h264BufSize;   // 缓冲区大小
unsigned int m_nCurPos;       // 当前读取位置

/**
 * @brief 从缓冲区读取一个NALU单元
 * @details 知识点：H264 NALU解析
 *          NALU（Network Abstraction Layer Unit）：H264的网络抽象层单元
 *          NALU起始码：0x00 0x00 0x00 0x01 或 0x00 0x00 0x01
 *          
 *          解析流程：
 *          1. 查找起始码（0x00 0x00 0x01 或 0x00 0x00 0x00 0x01）
 *          2. 找到下一个起始码，确定NALU边界
 *          3. 提取NALU数据
 * @param nalu 输出参数，NALU单元结构（包含数据和大小）
 * @return true=成功读取一个NALU，false=无更多NALU
 */
bool ReadOneNaluFromBuf(NalUnit *nalu)
{
    int i = m_nCurPos;
    
    // 知识点：查找NALU起始码
    // 起始码格式：
    //   短起始码：0x00 0x00 0x01（3字节）
    //   长起始码：0x00 0x00 0x00 0x01（4字节）
    while( i < m_h264BufSize )
    {
        // 检查是否找到起始码的前两个字节（0x00 0x00）
        if(m_h264Buf[i++] == 0x00 && m_h264Buf[i++] == 0x00)
        {
            unsigned char c = m_h264Buf[i++];
            // 检查是否为短起始码（0x01）或长起始码（0x00 0x01）
            if((c == 0x01) || ((c == 0) && (m_h264Buf[i++] == 0x01)) )
            {
                // 找到起始码，记录NALU开始位置
                int pos = i;
                int num = 1;  // 起始码长度（3或4字节）
                
                // 知识点：查找下一个NALU的起始码（确定当前NALU的结束位置）
                while( pos < m_h264BufSize )
                {
                    if(m_h264Buf[pos++] == 0x00 && m_h264Buf[pos++] == 0x00)
                    {
                        c = m_h264Buf[pos++];
                        if(c == 0x01)
                        {
                            num = 3;  // 短起始码（3字节）
                            break;
                        }
                        else if (c == 0 && (m_h264Buf[pos++] == 0x01) )
                        {
                            num = 4;  // 长起始码（4字节）
                            break;
                        }
                    }
                }
                if(pos == m_h264BufSize)
                {
                    nalu->size = pos-1;
                }
                else
                {
                    nalu->size = (pos-num)-1;
                }
                nalu->type = m_h264Buf[i&0x1f];
                nalu->data = &m_h264Buf[i];
                m_nCurPos = pos - num;
                return TRUE;
            }
        }
    }
    return FALSE;
}

bool SendChunkSize(int newSize)
{
    RTMPPacket packet;
    RTMPPacket_Alloc(&packet, 4);
    packet.m_packetType = RTMP_PACKET_TYPE_CHUNK_SIZE;
    packet.m_nChannel = 0x02;
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_nBodySize = 4;
    g_rtmp->m_outChunkSize = newSize;//0x001000
    
    packet.m_body[0] = newSize >> 24;
    packet.m_body[1] = newSize >> 16;
    packet.m_body[2] = newSize >> 8;
    packet.m_body[3] = newSize & 0xff;
    RTMP_SendPacket(g_rtmp, &packet, 0);
}

int SendPacket(unsigned int nPacketType,unsigned char *data,unsigned int size,unsigned int nTimeStamp)
{
    if(g_rtmp == NULL)
    {
        return FALSE;
    }
    
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);
    RTMPPacket_Alloc(&packet,size);
    packet.m_packetType = nPacketType;
    packet.m_nChannel = 0x04;
    packet.m_headerType = (nPacketType == RTMP_PACKET_TYPE_INFO)?RTMP_PACKET_SIZE_LARGE:RTMP_PACKET_SIZE_MEDIUM;
    packet.m_nTimeStamp = nTimeStamp;
    packet.m_nInfoField2 = g_rtmp->m_stream_id;
    packet.m_hasAbsTimestamp = 0;
    
    packet.m_nBodySize = size;
    memcpy(packet.m_body,data,size);
    int nRet = RTMP_SendPacket(g_rtmp,&packet,1);
    RTMPPacket_Free(&packet);
    return nRet;
}

bool SendH264Packet(unsigned char *data,unsigned int size,bool bIsKeyFrame,unsigned int nTimeStamp)
{
    if(data == NULL && size)
    {
        return FALSE;
    }
    
    unsigned char *body = (unsigned char*) malloc(size+9);
    int i = 0;
    body[i++] = (bIsKeyFrame?0x17:0x27); // 1:Iframe 7:AVC ,2:Pframe 7:AVC
    
    body[i++] = size>>24;
    body[i++] = size>>16;
    body[i++] = size>>8;
    body[i++] = size&0xff;
    
    // NALU data
    memcpy(&body[i],data,size);
    
    bool bRet = SendPacket(RTMP_PACKET_TYPE_VIDEO,body,i+size,nTimeStamp);
    if(body)
    {
        free(body);
    }
    return bRet;
}

bool SendSpsPps(LPRTMPMetadata lpMetaData)
{
    if(!lpMetaData == NULL)
    {
        return FALSE;
    }
    
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);
    RTMPPacket_Alloc(&packet,824);
    
    packet.m_nChannel = 0x04;//消息的第一个消息
    packet.m_packetType = RTMP_PACKET_TYPE_VIDEO;
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM; //对应rtmp Chunk basic Header中chunk type(fmt)字段。
    packet.m_nTimeStamp = 0; //没有时间戳
    packet.m_nInfoField2 = g_rtmp->m_stream_id;
    packet.m_hasAbsTimestamp = 0; //不使用绝对时间
    
    char * p = (char *)packet.m_body;
    int i = 0;
    p[i++] = 0x17; // 1:keyframe 7:AVC  协议头 0x17= 0001 0111 第四位为1 表示关键帧，第四位为7表示AVC.
    p[i++] = 0x00; // AVC sequence header
    p[i++] = 0x00; // composition time 合成时间
    p[i++] = 0x00;
    p[i++] = 0x00; // fill in 0;
    
    // AVDecoderConfigurationRecord.
    p[i++] = 0x01;                // configurationVersion 版本
    p[i++] = lpMetaData->Sps[1]; // AVCProfileIndication 下面几位是编码规格
    p[i++] = lpMetaData->Sps[2]; // profile_compatibility
    p[i++] = lpMetaData->Sps[3]; // AVCLevelIndication
    p[i++] = 0xff;                // lengthSizeMinusOne
    
    // sps nums
    p[i++] = 0x01; //&0x1f 1110 0001 表示SPS个数为1
    // sps data length  两个字节存储SPS长度
    p[i++] = lpMetaData->nSpsLen>>8;
    p[i++] = lpMetaData->nSpsLen&0xff;
    // sps data  SPS数据
    memcpy(&p[i], lpMetaData->Sps, lpMetaData->nSpsLen);
    i = i + lpMetaData->nSpsLen;
    
    // pps nums PPS个数
    p[i++] = 0x01; //&0x1f
    // pps data length  PPS长度 用两个字节存储PPS 长度
    p[i++] = lpMetaData->nPpsLen>>8;
    p[i++] = lpMetaData->nPpsLen&0xff;
    // PPS 内容
    memcpy(&p[i], lpMetaData->Pps, lpMetaData->nPpsLen);
    i = i + lpMetaData->nPpsLen;
    
    packet.m_nBodySize = i;
    
    RTMP_SendPacket(g_rtmp,&packet,0);
    RTMPPacket_Free(&packet);
}

bool SendAacCfgPack(char *pAacBuf)
{
    unsigned profileId = pAacBuf[2]>>6;
    unsigned sampleRate = (pAacBuf[2]>>2)&0x0f;
    unsigned channel = ((pAacBuf[2]&0x03)<<2)|(pAacBuf[3]>>6);
    
    RTMPPacket pack;
    RTMPPacket_Alloc(&pack, 4);
    pack.m_packetType = RTMP_PACKET_TYPE_AUDIO;
    pack.m_nChannel = 0x04;
    pack.m_headerType = RTMP_PACKET_SIZE_LARGE;
    pack.m_nTimeStamp = 0;
    pack.m_nInfoField2 = 1;
    pack.m_hasAbsTimestamp = 0;
    
    pack.m_body[0] = 0xaf; //双声道
    pack.m_body[1] = 0x00; //0x00 aac头信息   0x01 aac 原始数据
    pack.m_body[2] = (profileId<<3)|(sampleRate>>1);
    pack.m_body[3] = (sampleRate<<7)|(channel<<3);
    RTMP_SendPacket(g_rtmp,&pack,0);
    RTMPPacket_Free(&pack);
}

bool SendAacPack(char *pAacBuf, int aacLen,unsigned int nTimeStamp)
{
    int i = 0;
    RTMPPacket pack;
    RTMPPacket_Alloc(&pack, aacLen);
    pack.m_packetType = RTMP_PACKET_TYPE_AUDIO;
    pack.m_nChannel = 0x04;
    pack.m_headerType = RTMP_PACKET_SIZE_LARGE;
    pack.m_nTimeStamp = nTimeStamp;
    pack.m_nInfoField2 = 1;
    pack.m_hasAbsTimestamp = 0;
    
    pack.m_body[i++] = 0xaf;//双声道
    pack.m_body[i++] = 0x01; //0x00 aac头信息   0x01 aac 原始数据
    
    memcpy(&pack.m_body[i], pAacBuf+7, aacLen-7);
    
    pack.m_nBodySize = i + (aacLen - 7);
    RTMP_SendPacket(g_rtmp,&pack,0);
    RTMPPacket_Free(&pack);
}

bool rtmpAvPublish()
{
    unsigned int sendFrameCounter = 0;
    bool isSendMeta = FALSE;
    RTMPMetadata metaData;
    memset(&metaData,0,sizeof(RTMPMetadata));
    
    NalUnit naluUnit;
    unsigned int tick = 0;
    
    //h.264
    video_frame_info video_info;
    h264_stream_init();
    
    //aac
    char aacBuf[1024];
    int aacLen = 0;
    int aacCfgIsSend = 0;
    aac_stream_init();
    
    g_startTime = RTMP_GetTime();
    while(g_quit == FALSE)
    {
        memset(&video_info,0,sizeof(video_info));
        h264_stream_get(&video_info);
        printf("video len: %d %d\n",video_info.frame_len,tick);
        
        m_h264Buf = video_info.frame_data;
        m_h264BufSize = video_info.frame_len;
        m_nCurPos = 0;
        
        while(ReadOneNaluFromBuf(&naluUnit))
        {
            if(isSendMeta == FALSE)
            {
                if(metaData.nWidth == 0 || metaData.nHeight == 0 || metaData.nFrameRate == 0)
                {
                    h264_param v_param;
                    memset(&v_param, 0, sizeof(v_param));
                    if(0 == h264_stream_param_get(&v_param))
                    {
                        metaData.nWidth = v_param.width;
                        metaData.nHeight = v_param.height;
                        metaData.nFrameRate = v_param.fps;
                    }
                }
                
                if(naluUnit.type == H264NT_SPS)//sps
                {
                    metaData.nSpsLen = naluUnit.size;
                    memcpy(metaData.Sps,naluUnit.data,naluUnit.size);
                    printf("sps%d:\n",naluUnit.size);
                    int i = 0;
                    for(i = 0; i < naluUnit.size; i++)
                        printf("%02x ", metaData.Sps[i]);
                    printf("\n");
                }
                else if(naluUnit.type == H264NT_PPS)//pps
                {
                    metaData.nPpsLen = naluUnit.size;
                    memcpy(metaData.Pps,naluUnit.data,naluUnit.size);
                    printf("pps%d:\n",naluUnit.size);
                    int i = 0;
                    for(i = 0; i < naluUnit.size; i++)
                        printf("%02x ", metaData.Pps[i]);
                    printf("\n");
                }
                
                if(metaData.nSpsLen > 0 && metaData.nPpsLen > 0 && metaData.nWidth > 0 
                   && metaData.nHeight > 0 && metaData.nFrameRate > 0)
                {
                    SendChunkSize(8192);
                    //SendMetaData(&metaData);
                    SendSpsPps(&metaData);
                    isSendMeta = TRUE;
                }
            }
            else if(naluUnit.type == H264NT_SLICE || naluUnit.type == H264NT_SLICE_IDR)
            {
                tick = RTMP_GetTime() - g_startTime;
                bool bKeyFrame = (naluUnit.type == H264NT_SLICE_IDR) ? TRUE : FALSE;
                SendH264Packet(naluUnit.data,naluUnit.size,bKeyFrame,tick);
            }
        }
        
        //aac
        aacLen = aac_stream_get(aacBuf, 1024);
        if(aacCfgIsSend == 0)
        {
            SendAacCfgPack(aacBuf);
            aacCfgIsSend = 1;
        }
        
        if(aacCfgIsSend == 1)
        {
            tick = RTMP_GetTime() - g_startTime;
            aacLen = aac_stream_get(aacBuf, 1024);
            SendAacPack(aacBuf, aacLen, tick);
        }
    }
    
    h264_stream_deinit();
    aac_stream_deinit();
    
    return TRUE;
}

/*int main(int argc,char* argv[])
{
    signal(SIGINT, handler);
    
    rtmpStreamInit();
    printf("connect to %s\n",SERVER);
    if(rtmpConnectUrl(SERVER))
    {
        printf("rtmp start ok!\n");
        //6666
        rtmpAvPublish();
    }
    else
    {
        printf("rtmp start failed!\n");
    }
    
    rtmpStreamDeInit();
}*/
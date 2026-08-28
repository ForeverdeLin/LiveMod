#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "aacstream.h"
#include "h264stream.h"
#include "RTMP.h"

#define SERVER "rtmp://192.168.118.14/test/live"

RTMP* g_rtmp;
bool g_quit = FALSE;
uint32_t g_startTime;

void handler(int signo)
{
    printf("rtmp publish -------------------------exit-------------------\n");
    g_quit = TRUE;
}

void rtmpStreamInit(void)
{
    g_rtmp = RTMP_Alloc();
    RTMP_Init(g_rtmp);
    g_rtmp->Link.timeout = 10;
    g_rtmp->Link.lFlags |= RTMP_LF_LIVE;
}

void rtmpStreamDeInit(void)
{
    if(g_rtmp)
    {
        RTMP_Close(g_rtmp);
        RTMP_Free(g_rtmp);
        g_rtmp = NULL;
    }
}

bool rtmpConnectUrl(const char* url)
{
    int ret = 0;
    ret = RTMP_SetURL(g_rtmp, (char*)url);
    if(ret < 0)
    {
        return FALSE;
    }
    RTMP_EnableWrite(g_rtmp);//代表推流，不填代表收流
    
    ret = RTMP_Connect(g_rtmp, NULL);
    if(ret <= 0)
    {
        return FALSE;
    }
    
    ret = RTMP_ConnectStream(g_rtmp,0);
    if(ret <= 0)
    {
        return FALSE;
    }
    return TRUE;
}

bool SendMetaData(LPRTMPMetadata lpMetaData)
{
    if(lpMetaData == NULL)
    {
        return FALSE;
    }
    
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);
    
    RTMPPacket_Alloc(&packet,1024);
    
    packet.m_nChannel = 0x04;
    packet.m_packetType = RTMP_PACKET_TYPE_INFO;
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = g_rtmp->m_stream_id;
    packet.m_hasAbsTimestamp = 0;
    
    char * p = (char *)packet.m_body;
    char * pend = p + 1024;
    
    static const AVal av_setDataFrame = AVC("@setDataFrame");
    p = AMF_EncodeString(p, pend, &av_setDataFrame);
    
    static const AVal av_onMetaData = AVC("onMetaData");
    p = AMF_EncodeString(p, pend, &av_onMetaData);
    
    *p++ = AMF_ECMA_ARRAY;
    p = AMF_EncodeInt32(p, pend, 5);
    
    static const AVal av_duration = AVC("duration");
    p = AMF_EncodeNamedNumber(p, pend, &av_duration, 0);
    
    static const AVal av_width = AVC("width");
    p = AMF_EncodeNamedNumber(p, pend, &av_width, lpMetaData->nWidth);
    
    static const AVal av_height = AVC("height");
    p = AMF_EncodeNamedNumber(p, pend, &av_height, lpMetaData->nHeight);
    
    static const AVal av_framerate = AVC("framerate");
    p = AMF_EncodeNamedNumber(p, pend, &av_framerate, lpMetaData->nFrameRate);
    
    static const AVal av_videocodecid = AVC("videocodecid");
    p = AMF_EncodeNamedNumber(p, pend, &av_videocodecid, 7);
    
    *p++ = 0;
    *p++ = AMF_OBJECT_END;
    
    packet.m_nBodySize = p - packet.m_body;
    RTMP_SendPacket(g_rtmp,&packet,0);
    RTMPPacket_Free(&packet);
}

unsigned char m_h264Buf;
unsigned int m_h264BufSize;
unsigned int m_nCurPos;

bool ReadOneNaluFromBuf(NalUnit *nalu)
{
    int i = m_nCurPos;
    while( i < m_h264BufSize )
    {
        if(m_h264Buf[i++] == 0x00 && m_h264Buf[i++] == 0x00)
        {
            unsigned char c = m_h264Buf[i++];
            if((c == 0x01) || ((c == 0) && (m_h264Buf[i++] == 0x01)) )
            {
                //printf("hand found nalu#######################\n");
                int pos = i;
                int num = 1;
                while( pos < m_h264BufSize )
                {
                    if(m_h264Buf[pos++] == 0x00 && m_h264Buf[pos++] == 0x00)
                    {
                        c = m_h264Buf[pos++];
                        if(c == 0x01)
                        {
                            num = 3;
                            break;
                        }
                        else if (c == 0 && (m_h264Buf[pos++] == 0x01) )
                        {
                            num = 4;
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
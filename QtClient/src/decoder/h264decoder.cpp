/**
 * @file h264decoder.cpp
 * @brief H264视频解码器实现
 * @details 使用FFmpeg库将H.264编码数据解码为YUV420P格式
 *          知识点：FFmpeg解码器API、H.264解码流程、YUV格式处理
 */

#include "h264decoder.h"

#if !defined(MIN)
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

/**
 * @brief 构造函数
 * @details 知识点：FFmpeg解码器初始化流程
 *          1. 查找解码器（avcodec_find_decoder）
 *          2. 分配解码器上下文（avcodec_alloc_context3）
 *          3. 打开解码器（avcodec_open2）
 *          4. 分配视频帧（av_frame_alloc）
 *          
 *          注意：FFmpeg 4.0+版本已弃用av_register_all()，解码器会自动注册
 */
H264Decoder::H264Decoder()
{
    // 注意：FFmpeg高版本已弃用，会自动注册
    // av_register_all();
    // avcodec_register_all();

    // 初始化成员变量为NULL（避免野指针）
    m_pCodec = NULL;
    m_pCodecCtx = NULL;
    m_pVideoFrame = NULL;

    // 知识点：查找H.264解码器
    // avcodec_find_decoder - 根据编码器ID查找可用的解码器
    // AV_CODEC_ID_H264: H.264编码器标识符
    m_pCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if(m_pCodec == NULL)
    {
        printf("avcodec_find_decoder error.\n");
        return;
    }

    // 知识点：分配解码器上下文
    // avcodec_alloc_context3 - 为解码器分配上下文结构体
    // 上下文用于存储解码器的配置参数和状态
    m_pCodecCtx = avcodec_alloc_context3(m_pCodec);
    if(m_pCodecCtx == NULL)
    {
        printf("avcodec_alloc_context3 error.\n");
        return;
    }

    // 知识点：打开解码器
    // avcodec_open2 - 使用配置的参数打开解码器，进行实际初始化
    // 参数3：AVDictionary*，用于传递额外的解码选项（这里传NULL使用默认值）
    if(avcodec_open2(m_pCodecCtx,m_pCodec,NULL) < 0)
    {
        printf("avcodec_open2 error.\n");
        return;
    }

    // 知识点：分配视频帧
    // av_frame_alloc - 分配AVFrame结构体
    // AVFrame用于存储解码后的原始视频数据（YUV420P格式）
    m_pVideoFrame = av_frame_alloc();
    if(m_pVideoFrame == NULL)
    {
        printf("m_pVideoFrame error.\n");
    }
}

H264Decoder::~H264Decoder()
{
    if(m_pCodecCtx != NULL){

        avcodec_free_context(&m_pCodecCtx);//avcodec_close(m_pCodecCtx);
        m_pCodecCtx = NULL;
    }

    if(m_pVideoFrame != NULL){
        av_frame_free(&m_pVideoFrame);
        m_pVideoFrame = NULL;
    }
}

/**
 * @brief 解码H264数据包
 * @details 知识点：FFmpeg解码流程
 *          1. 封装数据为AVPacket（av_packet_alloc）
 *          2. 发送数据包到解码器（avcodec_send_packet）
 *          3. 接收解码后的帧（avcodec_receive_frame）
 *          4. 提取YUV数据到输出结构
 * @param pBuff H.264编码数据（包含NALU起始码）
 * @param length 数据长度（字节）
 * @param yuvFrame 输出参数，解码后的YUV420P数据
 * @return 0=成功，<0=失败
 */
int H264Decoder::DecodeH264Packet(unsigned char* pBuff, int length,YUVData_Frame* yuvFrame)
{
    // 知识点：封装输入数据为AVPacket
    // AVPacket - FFmpeg中编码后数据（压缩数据）的容器
    // av_packet_alloc - 分配AVPacket结构体
    AVPacket* pkt = av_packet_alloc();
    pkt->data = pBuff;   // 设置数据指针
    pkt->size = length;  // 设置数据大小

    // 知识点：发送数据包到解码器
    // avcodec_send_packet - 将AVPacket发送到解码器上下文
    // 注意：avcodec_decode_video2已被弃用，新API使用send/receive组合
    int ret = avcodec_send_packet(m_pCodecCtx, pkt);
    if (ret < 0) {
        av_packet_free(&pkt);  // 释放数据包
        return ret;
    }

    int gotPic = 0;  // 解码成功标志
    
    // 知识点：循环接收解码帧
    // avcodec_receive_frame - 从解码器接收解码后的视频帧
    // 注意：发送一个packet可能产生0个或多个帧，也可能需要发送多个packet才能产生一个帧
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(m_pCodecCtx, m_pVideoFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            // AVERROR(EAGAIN): 需要更多输入数据（正常情况）
            // AVERROR_EOF: 输入数据已耗尽（正常情况）
            break;
        }
        else if (ret < 0)
        {
            // 其他错误
            av_packet_free(&pkt);
            return ret;
        }
        gotPic = 1; // 解码成功标记
    }

    if(gotPic)
    {
        unsigned int lumaLength = (m_pCodecCtx->height ) * MIN(m_pVideoFrame->linesize[0], m_pCodecCtx->width);
        unsigned int chromaBLength = (m_pCodecCtx->height / 2) * MIN(m_pVideoFrame->linesize[1], m_pCodecCtx->width / 2);
        unsigned int chromaRLength = (m_pCodecCtx->height / 2) * MIN(m_pVideoFrame->linesize[2], m_pCodecCtx->width / 2);


        //printf("DECODE LEN... %d %d %d %d\n", m_pVideoFrame->linesize[0], m_pVideoFrame->linesize[1], m_pVideoFrame->linesize[2], m_pCodecCtx->width);

        yuvFrame->luma.dataBuffer = (unsigned char*)malloc(lumaLength);
        yuvFrame->chromaB.dataBuffer = (unsigned char*)malloc(chromaBLength);
        yuvFrame->chromaR.dataBuffer = (unsigned char*)malloc(chromaRLength);

        yuvFrame->luma.length = lumaLength;
        yuvFrame->chromaB.length = chromaBLength;
        yuvFrame->chromaR.length = chromaRLength;

        copyDecodedFrame(m_pVideoFrame->data[0], yuvFrame->luma.dataBuffer, m_pVideoFrame->linesize[0], m_pCodecCtx->width, m_pCodecCtx->height);
        copyDecodedFrame(m_pVideoFrame->data[1], yuvFrame->chromaB.dataBuffer, m_pVideoFrame->linesize[1], m_pCodecCtx->width / 2, m_pCodecCtx->height / 2);
        copyDecodedFrame(m_pVideoFrame->data[2], yuvFrame->chromaR.dataBuffer, m_pVideoFrame->linesize[2], m_pCodecCtx->width / 2, m_pCodecCtx->height / 2);
        //m_pVideoFrame 解码后的数据会做字节对齐，m_pCodecCtx->width 是视频的实际有效宽度（不含对齐冗余）。
        yuvFrame->width = m_pCodecCtx->width;
        yuvFrame->height = m_pCodecCtx->height;
    }

    av_packet_free(&pkt);//无论解码成功与否，最终调用 av_packet_free 释放 AVPacket 的内存，避免内存泄漏。
    return gotPic ? 0 : -1;
}

void H264Decoder::copyDecodedFrame(unsigned char* src, unsigned char* dist, int linesize, int width, int height)
{//源数据地址，目标数据地址，源数据每行的字节数，每行有效数据的宽度，   数据的高度（总行数）（视频实际高度，与分辨率相关）
    width = MIN(linesize, width);
    for(int i=0; i < height; i++){
        memcpy(dist, src, width);//填充到四的倍数的多余填充数据在数据末尾
        dist += width;//意思就是换行
        src += linesize;
    }
}

#ifndef H264DECODER_H
#define H264DECODER_H

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}


#include "CCYUVDataDefine.h"

class H264Decoder
{
public:
    H264Decoder();
    ~H264Decoder();

    int DecodeH264Packet(unsigned char* pBuff, int length,YUVData_Frame* outBuff);

private:
    const AVCodec*        m_pCodec;   //解码器
    AVCodecContext* m_pCodecCtx;//上下文
    AVFrame*        m_pVideoFrame;//解码后的yuv数据,解码前的h264数据放在av parket里


private:
    void copyDecodedFrame(unsigned char* src, unsigned char* dist, int linesize, int width, int height);

};

#endif // H264DECODER_H

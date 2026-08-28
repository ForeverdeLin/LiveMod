#ifndef CCVIDEOWRITER_H
#define CCVIDEOWRITER_H

#include <iostream>
#include <pthread.h>
#include <mutex>
#include <string>
#include <chrono>
#include <ratio>
#include "faac.h"

extern "C"{
    #include <libavcodec/avcodec.h>
    #include <libavdevice/avdevice.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
    #include <libavutil/frame.h>
    #include <libavutil/imgutils.h>
}

#define AV_MAX_AUDIO_DATA_SIZE 1280
#define AV_MAX_VIDEO_DATA_SIZE 64 * 1024

#ifndef CODEC_FLAG_GLOBAL_HEADER
#define CODEC_FLAG_GLOBAL_HEADER (1 << 22)
#endif

// H.264 NAL type
typedef enum//枚举：定义 H.264 NALU 类型,比如0是NAL类型，1是SLICE
{
    H264NT_NAL = 0,
    H264NT_SLICE,
    H264NT_SLICE_DPA,
    H264NT_SLICE_DPB,
    H264NT_SLICE_DPC,
    H264NT_SLICE_IDR,
    H264NT_SEI,
    H264NT_SPS,
    H264NT_PPS
} H264_nalType;

// NALU 描述一个具体的 NALU 单元
typedef struct Mp4NaluUnit
{
    int type;
    int size;
    unsigned char *data;
} H264_NaluUnit;

typedef struct mp4AVCC_Box
{
    int sps_length;
    int pps_length;
    unsigned char spsBuffer[128];
    unsigned char ppsBuffer[64];
}H264_RecordAvccBox;

typedef struct tag_RECORD_INFO
{
    int m_nCurPts;          //当前位置的pts
    double m_nCurTime;      //当前位置的时间（没有使用
    uint m_nTotalTime;      //总时长
    uint m_nLastTimeStamp;  //上一个位置的时间戳

}RecordInfo;

typedef struct
{
    int            m_nCodecID;
    unsigned int   m_nTimeStamp;
    unsigned int   m_nSize;
    unsigned char  m_pDataBuff[AV_MAX_VIDEO_DATA_SIZE];
}H264_FrameData;

typedef struct
{
    int            m_nCodecID;
    unsigned int   m_nTimeStamp;
    unsigned int   m_nSize;
    unsigned char  m_pDataBuff[AV_MAX_AUDIO_DATA_SIZE];
}Audio_FrameData;

typedef struct MP4_AAC_CONFIGURE
{
    faacEncHandle hEncoder;      //音频文件描述符
    unsigned int  nSampleRate;   //音频采样数
    unsigned int  nChannels;     //音频声道数
    unsigned int  nPCMBufferSize;//每次调用编码时所应接收的原始数据长度
    unsigned int  nMaxOutputBytes;//每次调用编码时生成的AAC数据的最大长度
    unsigned char* pcmBuffer;    //pcm数据
    unsigned char* aacBuffer;    //aac数据
}AACEncodeConfig;





class CCVideoWriter
{
public:
    static CCVideoWriter* GetInstance();
    bool StartVideoWriterWithPath(std::string filePath);
    void StopWriteReleaseResources();
    void SetVideoSize(int width, int height);
    void WriteVideoData(unsigned char* pBuff, int length);
    void WriteAudioData(unsigned char* pBuff, int length);
    bool writeVideoFrame(H264_FrameData* pData, int keyFlags);

private:
    AVStream*   initVideoStreamInfo();
    AVStream*   initAudioStreamInfo();

    bool        initVideoWriter();
    bool        writeVideoHeader();

    void        resetAVDataInfomation();

    void        getIndexConfigure(unsigned int aSample,unsigned int aChannels,unsigned char* indexBuff);
    int         getSampleIndex(unsigned int aSamples);

    bool readOneNaluFromBuff(unsigned char* buffer, unsigned int nBufferSize, unsigned int aOffSet, H264_NaluUnit *nalu);
    uint64_t elapseMS(std::chrono::system_clock::time_point &startTime);
    AACEncodeConfig* initAudioEncodeConfiguration();

    bool writeAudioStream(Audio_FrameData* pData);
    int LinearPCM2AAC(unsigned char* pData, int aSize);

private:
    bool m_bRecordStatus;
    bool m_bStartRecordStatus;

    int m_videoWidth;
    int m_videoHeight;
    int m_nFrameRate;

    std::string     m_fileString;
    int m_nFileTotalSize; //文件总大小

    RecordInfo  m_stFrameInfo;//视频信息
    RecordInfo  m_stAudioInfo;//音频信息

    AVStream*       m_pVideoStream;
    AVStream*       m_pAudioStream;
    AVFormatContext* m_pFormatContext;

    H264_RecordAvccBox m_avcCBox;

    unsigned int m_nCurrNALUPos;
    bool m_bFirstIFrame;

    std::mutex m_vmMutex;
    std::chrono::system_clock::time_point m_startTimeStamp;//C++11提供的跨平台时间戳

    AACEncodeConfig* g_aacEncodeConfig;

private:
    CCVideoWriter();
    //~CCVideoWriter();
    static CCVideoWriter* m_pInstance;
    //static pthread_mutex_t m_mutex;
    static std::mutex m_mutex;

    class Garbage{
        public:
            ~Garbage(){
                if(CCVideoWriter::m_pInstance != nullptr){
                    delete CCVideoWriter::m_pInstance;
                    CCVideoWriter::m_pInstance=nullptr;
                }
            }
    };

    static Garbage  m_garbage;

};

#endif

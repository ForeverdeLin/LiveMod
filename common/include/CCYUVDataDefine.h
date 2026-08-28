#ifndef CCYUVDATADEFINE_H
#define CCYUVDATADEFINE_H

#include <stdint.h>

#pragma pack(push, 1)

typedef struct _YUVChannel
{
    unsigned int length;
    unsigned char* dataBuffer;
} YUVChannel;

typedef struct _YUVFrame
{
    unsigned int width;
    unsigned int height;
    YUVChannel   luma;      // Y
    YUVChannel   chromaB;   // U
    YUVChannel   chromaR;   // V
    int64_t      pts;
} YUVData_Frame;

#pragma pack(pop)

#endif // CCYUVDATADEFINE_H

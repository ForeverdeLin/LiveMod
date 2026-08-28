#ifndef CCYUVDATADEFINE_H
#define CCYUVDATADEFINE_H

#include <stdint.h>
#include <stdio.h>

#pragma pack(push,1)

typedef struct YUVChannelDef//一个分量
{
    unsigned int    length;
    unsigned char*  dataBuffer;
}YUVChannel;

typedef struct YUVFrameDef
{
    unsigned int    width;
    unsigned int    height;
    YUVChannel      luma;//Y
    YUVChannel      chromaB;//U
    YUVChannel      chromaR;//V
    long long       pts;
}YUVData_Frame;

#pragma pack(pop)



#endif // CCYUVDATADEFINE_H

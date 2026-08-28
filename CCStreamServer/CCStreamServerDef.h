#ifndef CCSTREAMSERVERDEF_H
#define CCSTREAMSERVERDEF_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <errno.h>
#include <netdb.h>
#include <sys/uio.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include"vector"
#include <atomic>
#include "CCThread.h"
#include "JCQueueDef.h"

# define LISTEN_PORT      30000
#define MSGHEADER_TYPE_KEEPALIVE    0
#define MSGHEADER_TYPE_AVSTREAM   1

#define CONTENT_AVSTREAM_VIDEO    3
#define CONTENT_AVSTREAM_AUDIO    4

#pragma pack(push,1)//字节对齐


typedef struct netMessageHeader
{
    char headerID[4];
    uint16_t msgType;//心跳还是信息
    uint16_t subType;//视频或音频
    uint16_t length;
    int64_t timestamp;  // 时间戳（毫秒），用于音视频同步

}CC_MsgHeader;

typedef struct netAVStream
{
    uint8_t* buffer;
    uint32_t size;
    uint16_t type;
    int64_t timestamp;  // 时间戳（毫秒），用于音视频同步
}CC_AVStream;


#pragma pack(pop)

#endif

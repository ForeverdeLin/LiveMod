#ifndef CCSOCKETDEFINE_H
#define CCSOCKETDEFINE_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>
#include <ifaddrs.h>
#include <stdbool.h>
#include <atomic>
#include <mutex>
#include <iterator>
#include <sys/wait.h>
#include <sys/stat.h>
#include <vector>
#include "err.h"

#define MSGHEADER_TYPE_KEEPALIVE    0
#define MSGHEADER_TYPE_AVSTREAM     1
#define CONTENT_AVSTREAM_VIDEO      3
#define CONTENT_AVSTREAM_AUDIO      4

#pragma pack(push,1)
typedef struct netConnectInfo
{
    char server_ip[16];
    int port;
}CC_NetConnectInfo;

typedef struct netMessageHeader
{
    char headerID[4];
    uint16_t msgType;//心跳还是信息
    uint16_t subType;//视频或音频
    uint16_t length;
}CC_MsgHeader;

#pragma pack(pop)

#endif
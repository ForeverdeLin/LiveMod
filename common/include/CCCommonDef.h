#ifndef CCCOMMONDEF_H
#define CCCOMMONDEF_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Protocol Constants
// ============================================================================

#define MSGHEADER_TYPE_KEEPALIVE    0
#define MSGHEADER_TYPE_AVSTREAM     1
#define CONTENT_AVSTREAM_VIDEO      3
#define CONTENT_AVSTREAM_AUDIO     4

// ============================================================================
// Network Structures
// ============================================================================

#pragma pack(push, 1)

typedef struct _CC_NetConnectInfo
{
    char server_ip[16];
    int  port;
} CC_NetConnectInfo;

typedef struct _CC_MsgHeader
{
    char     headerID[4];
    uint16_t msgType;    // MSGHEADER_TYPE_KEEPALIVE or AVSTREAM
    uint16_t subType;    // CONTENT_AVSTREAM_VIDEO or AUDIO
    uint16_t length;
    int64_t  timestamp;  // milliseconds, for A/V sync
} CC_MsgHeader;

typedef struct _CC_AVStream
{
    uint8_t* buffer;
    uint32_t size;
    uint16_t type;
    int64_t  timestamp;  // milliseconds, for A/V sync
} CC_AVStream;

#pragma pack(pop)

// ============================================================================
// Thread Utilities
// ============================================================================

typedef void* (*ThreadFunc)(void*);

int cc_detach_thread_create(void* thread, ThreadFunc start_routine, void* arg);

#endif // CCCOMMONDEF_H

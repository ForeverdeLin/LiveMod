// Unit tests for common/include/CCCommonDef.h
// Validates wire-protocol structure layouts and packed-field semantics.

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

#include "CCCommonDef.h"

namespace {

// CC_NetConnectInfo: server_ip[16] + int (port)
constexpr size_t kNetConnectInfoExpected = 16 + sizeof(int);

TEST(CCCommonDefTest, NetConnectInfoPackedLayout) {
    EXPECT_EQ(sizeof(CC_NetConnectInfo), kNetConnectInfoExpected)
        << "CC_NetConnectInfo must be byte-packed (no alignment padding)";
}

// CC_MsgHeader: 4-byte ID + 3 * uint16_t + int64_t timestamp
constexpr size_t kMsgHeaderExpected =
    4 + sizeof(uint16_t) * 3 + sizeof(int64_t);

TEST(CCCommonDefTest, MsgHeaderPackedLayout) {
    EXPECT_EQ(sizeof(CC_MsgHeader), kMsgHeaderExpected)
        << "CC_MsgHeader must be byte-packed; any padding would break the wire format";
}

TEST(CCCommonDefTest, MsgHeaderFieldOffsets) {
    CC_MsgHeader h{};
    EXPECT_EQ(offsetof(CC_MsgHeader, headerID),  0u);
    EXPECT_EQ(offsetof(CC_MsgHeader, msgType),   4u);
    EXPECT_EQ(offsetof(CC_MsgHeader, subType),   6u);
    EXPECT_EQ(offsetof(CC_MsgHeader, length),    8u);
    EXPECT_EQ(offsetof(CC_MsgHeader, timestamp), 10u);
}

TEST(CCCommonDefTest, MsgHeaderMagicIDIsAssignable) {
    CC_MsgHeader h{};
    std::memcpy(h.headerID, "CCTC", 4);
    EXPECT_EQ(h.headerID[0], 'C');
    EXPECT_EQ(h.headerID[3], 'C');
}

TEST(CCCommonDefTest, MsgTypeConstantsAreUnique) {
    EXPECT_NE(MSGHEADER_TYPE_KEEPALIVE, MSGHEADER_TYPE_AVSTREAM);
    EXPECT_NE(CONTENT_AVSTREAM_VIDEO,   CONTENT_AVSTREAM_AUDIO);
}

// CC_AVStream is intentionally NOT packed (size_t pointers, host-only struct).
TEST(CCCommonDefTest, AVStreamHasExpectedMembers) {
    CC_AVStream s{};
    s.buffer    = reinterpret_cast<uint8_t*>(0x1000);
    s.size      = 1024;
    s.type      = CONTENT_AVSTREAM_VIDEO;
    s.timestamp = 42;

    EXPECT_EQ(s.buffer, reinterpret_cast<uint8_t*>(0x1000));
    EXPECT_EQ(s.size,   1024u);
    EXPECT_EQ(s.type,   CONTENT_AVSTREAM_VIDEO);
    EXPECT_EQ(s.timestamp, 42);
}

}  // namespace

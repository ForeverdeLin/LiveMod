// Unit tests for common/include/CCYUVDataDefine.h
// Validates YUV frame layout used by the Qt client decoder.

#include <gtest/gtest.h>
#include <cstdint>

#include "CCYUVDataDefine.h"

namespace {

// YUVData_Frame is packed: uint + uint + uint + (uint+ptr) + (uint+ptr) + (uint+ptr) + int64
// Plus pointer alignment on most platforms; we just check it's >= the sum of fields.
constexpr size_t kYUVMinSize =
    sizeof(unsigned int) * 4 +               // width, height, luma.length, chromaB.length, chromaR.length
    sizeof(unsigned char*) * 3 +             // luma/chromaB/chromaR dataBuffer
    sizeof(int64_t);                         // pts

TEST(YUVDataDefineTest, FrameIsNonEmpty) {
    EXPECT_GE(sizeof(YUVData_Frame), kYUVMinSize);
}

TEST(YUVDataDefineTest, ChannelFieldsAccessible) {
    YUVChannel ch{};
    ch.length     = 64u;
    ch.dataBuffer = reinterpret_cast<unsigned char*>(0xABCDu);
    EXPECT_EQ(ch.length, 64u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ch.dataBuffer), 0xABCDu);
}

TEST(YUVDataDefineTest, FrameHasThreePlanes) {
    YUVData_Frame f{};
    f.width  = 1920;
    f.height = 1080;
    f.luma.length    = 1920u * 1080u;
    f.chromaB.length = 1920u * 1080u / 4u;
    f.chromaR.length = 1920u * 1080u / 4u;
    f.pts = 123456789;

    EXPECT_EQ(f.width,  1920u);
    EXPECT_EQ(f.height, 1080u);
    EXPECT_EQ(f.luma.length,    1920u * 1080u);
    EXPECT_EQ(f.chromaB.length, 1920u * 1080u / 4u);
    EXPECT_EQ(f.chromaR.length, 1920u * 1080u / 4u);
    EXPECT_GT(f.chromaB.length, 0u);
    EXPECT_GT(f.chromaR.length, 0u);
    EXPECT_EQ(f.pts, 123456789);
}

}  // namespace

# Tests for common/include/

if(NOT TARGET GTest::gtest_main)
    return()
endif()

livemod_add_test(test_protocol test_protocol.cpp)
livemod_add_test(test_yuv      test_yuv.cpp)

cmake_minimum_required(VERSION 3.22)
project(CCVideoClient VERSION 1.0)





SET(FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mac/libFFmpeg)
SET(X264_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mac/libx264)
SET(FFAAC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mac/libfaac)

INCLUDE_DIRECTORIES("/usr/include")
LINK_DIRECTORIES("/usr/lib")

INCLUDE_DIRECTORIES(${FFMPEG_DIR}/include)
LINK_DIRECTORIES(${FFMPEG_DIR}/lib)
LINK_DIRECTORIES(${X264_DIR}/lib)

INCLUDE_DIRECTORIES(${FFAAC_DIR}/include)
LINK_DIRECTORIES(${FFAAC_DIR}/lib)


SET(FFMPEG_LIB avformat avcodec avdevice avfilter avutil swresample swscale fdk-aac x264 z bz2 lzma iconv)

SET(CPP_SOURCE CCVideoClient.cpp
               main.cpp
               CCThread.cpp
               CCVideoWriter.cpp
               FFmpegRTMPReceiver.cpp)

add_executable(CCVideoClient ${CPP_SOURCE})
target_link_libraries(CCVideoClient ${FFMPEG_LIB} pthread)
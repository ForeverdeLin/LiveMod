cmake_minimum_required(VERSION 3.22)
project(CCStreamServer VERSION 1.0)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(PkgConfig REQUIRED)

# FFmpeg
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale libswresample)

# OpenCV
find_package(OpenCV REQUIRED)

# libwebsockets (WebSocket服务器)
pkg_check_modules(LWS libwebsockets)

# x264 (软件编码器)
find_library(X264_LIB x264)

INCLUDE_DIRECTORIES("/usr/include")
LINK_DIRECTORIES("/usr/lib")

# common
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/../common/include)

# 包含FFmpeg头文件目录
INCLUDE_DIRECTORIES(${FFMPEG_INCLUDE_DIRS})
INCLUDE_DIRECTORIES(${OpenCV_INCLUDE_DIRS})

# 源文件列表（新增模块）
SET (CPP_SOURCE H264Capture.cpp 
                H264Encoder.cpp 
                CCThread.cpp
                CCStreamServer.cpp
                CCClientProcess.cpp
                CCServerController.cpp
                FFmpegRTMPSender.cpp
                HWVideoEncoder.cpp
                BeautyFilter.cpp
                WebSocketControlServer.cpp
                ALSAAudioCapture.cpp
                AACEncoder.cpp
                ThreadPool.cpp
                main.cpp )

add_executable(CCStreamServer ${CPP_SOURCE})

# ALSA库
find_library(ASOUND_LIB asound)

# 链接库
target_link_libraries(CCStreamServer 
    ${X264_LIB}
    ${FFMPEG_LIBRARIES}
    ${OpenCV_LIBS}
    ${LWS_LIBRARIES}
    ${ASOUND_LIB}
    pthread
)

# 编译选项
target_compile_options(CCStreamServer PRIVATE ${FFMPEG_CFLAGS_OTHER})
target_compile_options(CCStreamServer PRIVATE ${LWS_CFLAGS_OTHER})
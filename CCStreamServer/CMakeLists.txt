cmake_minimum_required(VERSION 3.22)
project(CCStreamServer VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale libswresample)
find_package(OpenCV REQUIRED)
pkg_check_modules(LWS libwebsockets)
find_library(X264_LIB x264)
find_library(ASOUND_LIB asound)

INCLUDE_DIRECTORIES("/usr/include")
LINK_DIRECTORIES("/usr/lib")

# common
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/../common/include)

INCLUDE_DIRECTORIES(${FFMPEG_INCLUDE_DIRS})
INCLUDE_DIRECTORIES(${OpenCV_INCLUDE_DIRS})

# src/ subdirectories
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/core)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/capture)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/encoder)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/filter)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/transport)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/control)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/thread)

file(GLOB_RECURSE CPP_SRC
    "src/core/*.cpp"
    "src/capture/*.cpp"
    "src/encoder/*.cpp"
    "src/filter/*.cpp"
    "src/transport/*.cpp"
    "src/control/*.cpp"
    "src/thread/*.cpp"
)

add_executable(CCStreamServer ${CPP_SRC})

target_link_libraries(CCStreamServer
    ${X264_LIB}
    ${FFMPEG_LIBRARIES}
    ${OpenCV_LIBS}
    ${LWS_LIBRARIES}
    ${ASOUND_LIB}
    pthread
)

target_compile_options(CCStreamServer PRIVATE ${FFMPEG_CFLAGS_OTHER})
target_compile_options(CCStreamServer PRIVATE ${LWS_CFLAGS_OTHER})

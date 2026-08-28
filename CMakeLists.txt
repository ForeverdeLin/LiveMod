cmake_minimum_required(VERSION 3.22)

# ============================================================================
# LiveMod - Aggregated build
# ============================================================================

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LIVEMOD_VERSION_RAW)
string(STRIP "${LIVEMOD_VERSION_RAW}" LIVEMOD_VERSION)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" LIVEMOD_VERSION_MATCH "${LIVEMOD_VERSION}")
if(NOT LIVEMOD_VERSION STREQUAL LIVEMOD_VERSION_MATCH)
    message(FATAL_ERROR "Invalid VERSION file: '${LIVEMOD_VERSION}'. Expected semver like '1.0.0'.")
endif()

project(LiveMod
    VERSION "${LIVEMOD_VERSION}"
    DESCRIPTION "RTMP stream media server + Qt client + MP4 recorder"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(LIVEMOD_BUILD_SERVER     "Build CCStreamServer"           ON)
option(LIVEMOD_BUILD_CLIENT_MP4 "Build ClientToMp4 recorder"     ON)
option(LIVEMOD_BUILD_QT_CLIENT  "Build Qt-based desktop client"  OFF)
option(LIVEMOD_BUILD_TESTS      "Build unit tests"               ON)
option(LIVEMOD_ENABLE_SANITIZER "Enable AddressSanitizer in Debug" OFF)

if(LIVEMOD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LIVEMOD_BUILD_SERVER)
    add_subdirectory(CCStreamServer)
endif()

if(LIVEMOD_BUILD_CLIENT_MP4)
    add_subdirectory(ClientToMp4)
endif()

if(LIVEMOD_BUILD_QT_CLIENT)
    if(NOT TARGET Qt6::Core)
        message(WARNING "Qt6 not found. Set CMAKE_PREFIX_PATH to Qt installation dir, or disable LIVEMOD_BUILD_QT_CLIENT.")
    else()
        add_subdirectory(QtClient)
    endif()
endif()

message(STATUS "")
message(STATUS "============================================================")
message(STATUS " LiveMod ${LIVEMOD_VERSION}")
message(STATUS "   CCStreamServer:        ${LIVEMOD_BUILD_SERVER}")
message(STATUS "   ClientToMp4:           ${LIVEMOD_BUILD_CLIENT_MP4}")
message(STATUS "   QtClient:              ${LIVEMOD_BUILD_QT_CLIENT}")
message(STATUS "   Tests:                 ${LIVEMOD_BUILD_TESTS}")
message(STATUS "============================================================")
message(STATUS "")

/**
 * @file main.cpp
 * @brief 录制客户端主程序入口          //服务器端有自制SRS，TCP,websocket三个服务器
 * @details 支持两种录制模式：
 *          1. TCP模式：从流媒体服务器TCP连接接收H.264/AAC数据并录制为MP4
 *          2. RTMP模式：从RTMP服务器拉流并录制为MP4
 *          知识点：命令行参数解析、TCP Socket客户端、RTMP拉流、MP4录制
 */

#include "CCVideoClient.h"
#include "FFmpegRTMPReceiver.h"
#include "CCVideoWriter.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

/**
 * @brief 主函数
 * @details 知识点：命令行参数处理
 *          根据参数选择TCP模式或RTMP模式
 *          
 *          用法：argv[0] = 程序名（./ClientToMp4）
 *          argv[1] = 模式选择（"tcp" 或 "rtmp"）
 *          argv[2] 及之后 = 该模式所需的参数
 *          - TCP模式: ./program tcp <server_ip> <port>
 *          ./ClientToMp4 tcp 192.168.1.100 30000
 *          - RTMP模式: ./program rtmp <rtmp_url>
 *          ./ClientToMp4 rtmp rtmp://server/app/stream
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 0=成功，1=失败
 */
int main(int argc, char* argv[])
{
    printf("=== 智能直播录制客户端 ===\n");
    printf("用法:\n");
    printf("  TCP模式:  %s tcp <server_ip> <port>\n", argv[0]);
    printf("  RTMP模式: %s rtmp <rtmp_url>\n", argv[0]);
    printf("示例:\n");
    printf("  %s tcp 192.168.236.101 30000\n", argv[0]);
    printf("  %s rtmp rtmp://live.bilibili.com/...\n", argv[0]);
    printf("\n");

    if (argc < 2) {
        printf("错误：缺少参数\n");
        return 1;
    }

    const char* mode = argv[1];

    if (strcmp(mode, "tcp") == 0) 
    {
        // 知识点：TCP模式
        // 从流媒体服务器TCP连接接收H.264/AAC数据并录制
        if (argc < 4) {
            printf("错误：TCP模式需要服务器IP和端口\n");
            return 1;
        }

        // 知识点：配置网络连接信息
        CC_NetConnectInfo networkInfo;
        memset(&networkInfo, 0, sizeof(CC_NetConnectInfo));

        strncpy(networkInfo.server_ip, argv[2], sizeof(networkInfo.server_ip));  // 服务器IP
        networkInfo.port = atoi(argv[3]);  // 服务器端口

        printf("TCP模式：连接到 %s:%d\n", networkInfo.server_ip, networkInfo.port);

        // 知识点：创建TCP客户端并开始连接
        // CCVideoClient - TCP客户端类，负责接收音视频流
        CCVideoClient videoClient;
        videoClient.StartSocketConnection(&networkInfo);  // 启动TCP连接

        // 等待录制（按Ctrl+C退出）
        // CCVideoClient内部会启动线程接收数据并写入MP4
        while (1) 
        {
            sleep(1);
        }
    }







    
    else if (strcmp(mode, "rtmp") == 0) 
    {
        // 知识点：RTMP模式
        // 从RTMP服务器拉流并录制为MP4文件
        if (argc < 3) {
            printf("错误：RTMP模式需要RTMP地址\n");
            return 1;
        }

        const char* rtmpUrl = argv[2];
        printf("RTMP模式：拉取流 %s\n", rtmpUrl);

        // 知识点：创建RTMP接收器
        // FFmpegRTMPReceiver - 使用FFmpeg实现RTMP拉流
        FFmpegRTMPReceiver receiver;

        // 知识点：Lambda回调函数
        // H.264数据回调：当接收到视频数据时调用，写入MP4文件
        auto h264Callback = [](uint8_t* data, int size, bool isKeyFrame, int64_t pts) {
            // 写入MP4文件
            // CCVideoWriter - 单例模式的MP4写入器
            CCVideoWriter::GetInstance()->WriteVideoData(data, size);
        };

        // AAC数据回调：当接收到音频数据时调用，写入MP4文件
        auto aacCallback = [](uint8_t* data, int size, int64_t pts) {
            // 写入MP4文件
            CCVideoWriter::GetInstance()->WriteAudioData(data, size);
        };

        // 知识点：开始RTMP拉流
        // StartReceive - 连接到RTMP服务器并开始接收流
        // 参数：RTMP URL、视频回调、音频回调
        if (!receiver.StartReceive(rtmpUrl, h264Callback, aacCallback)) {
            printf("启动RTMP拉流失败\n");
            return 1;
        }

        // 知识点：获取视频信息并设置录制参数
        // 从RTMP流中获取视频分辨率、帧率等信息
        int width, height, fps;
        receiver.GetVideoInfo(width, height, fps);
        CCVideoWriter::GetInstance()->SetVideoSize(width, height);  // 设置MP4视频参数
        
        // 知识点：生成带时间戳的文件名
        // 格式：record_YYYYMMDD_HHMMSS.mp4
        char filename[256];
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        snprintf(filename, sizeof(filename), "record_%04d%02d%02d_%02d%02d%02d.mp4",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        
        // 知识点：启动MP4录制
        // StartVideoWriterWithPath - 创建MP4文件并开始写入
        CCVideoWriter::GetInstance()->StartVideoWriterWithPath(filename);
        printf("开始录制到文件: %s\n", filename);

        // 等待录制（按Ctrl+C退出）
        // 循环检查接收状态，直到RTMP连接断开
        while (receiver.IsReceiving()) {
            sleep(1);
        }

        // 知识点：停止录制并释放资源
        receiver.StopReceive();  // 停止RTMP拉流
        CCVideoWriter::GetInstance()->StopWriteReleaseResources();  // 关闭MP4文件
    }
    else {
        printf("错误：未知模式 '%s'\n", mode);
        return 1;
    }

    return 0;
}
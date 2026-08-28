/**
 * @file main.cpp
 * @brief 流媒体服务器主程序入口
 * @details 创建服务器控制器并启动流媒体服务
 */

#include<stdio.h>

#include "CCServerController.h"

/**
 * @brief 主函数
 * @details 程序入口点
 *          创建CCServerController对象并启动服务器
 * @return 0=正常退出
 */
int main()
{
    // 创建服务器控制器对象
    // CCServerController负责管理整个流媒体服务器的生命周期
    CCServerController serverController;
    
    // 启动服务器控制器
    // 该方法会启动所有必要的服务（音频采集、视频编码、RTMP推流等）
    serverController.StartServerController();
    
    return 0;
}

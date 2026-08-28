/**
 * @file main.cpp
 * @brief Qt视频客户端主程序入口
 * @details 使用Qt框架创建GUI应用程序，接收并播放流媒体服务器的音视频流
 *          知识点：Qt应用程序框架、GUI编程、音视频解码和播放
 */

#include "mainwindow.h"
#include "h264decoder.h"
#include <QApplication>
#include <iostream>

extern "C" {
#include <libavutil/version.h>
#include <libavcodec/version.h>
#include <libavformat/version.h>
}

/**
 * @brief 主函数
 * @details 知识点：Qt应用程序启动流程
 *          1. 创建QApplication对象（管理应用程序生命周期）
 *          2. 创建主窗口（MainWindow）
 *          3. 显示窗口（show）
 *          4. 进入事件循环（exec）
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 应用程序退出码
 */
int main(int argc, char *argv[])
{
    // 知识点：创建Qt应用程序对象
    // QApplication - Qt应用程序核心类，管理事件循环、窗口系统等
    QApplication a(argc, argv);
    
    // 知识点：创建主窗口
    // MainWindow - 自定义主窗口类，包含视频显示、连接控制等功能
    MainWindow w;//进入mainwindow构造函数
    w.show();  // 显示窗口
    
    // 知识点：进入事件循环
    // exec - 启动Qt事件循环，处理用户交互、定时器、网络事件等
    // 阻塞直到应用程序退出
    return a.exec();
}

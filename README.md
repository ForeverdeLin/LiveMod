# LiveMod - 流媒体传输与录制系统

RTMP 流媒体服务器 + Qt 客户端 + MP4 录制工具。

## 项目架构

```
LiveMod/
├── common/              # 公共类型定义（所有模块共享）
│   └── include/
│       ├── CCCommonDef.h    # 网络协议结构体（CC_MsgHeader, CC_AVStream）
│       ├── CCQueue.h        # 线程安全队列
│       ├── CCRTMPDef.h      # RTMP/H.264/AAC 类型定义
│       └── CCYUVDataDefine.h
│
├── CCStreamServer/      # 流媒体服务器
│   ├── src/
│   │   ├── core/        # 服务器核心（CCStreamServer, CCServerController, CCClientProcess）
│   │   ├── capture/     # 音视频采集（H264Capture, ALSAAudioCapture）
│   │   ├── encoder/     # 编码器（H264Encoder, HWVideoEncoder, AACEncoder）
│   │   ├── filter/      # 美颜滤镜（BeautyFilter）
│   │   ├── transport/   # RTMP 传输（FFmpegRTMPSender）
│   │   ├── control/     # WebSocket 控制接口
│   │   └── thread/      # 线程池（ThreadPool, CCThread）
│   └── include/         # 公共类型 wrapper（向后兼容）
│
├── QtClient/            # Qt 桌面客户端
│   ├── src/
│   │   ├── core/       # 主窗口（MainWindow）
│   │   ├── decoder/     # 音视频解码（h264decoder, aacdecoder）
│   │   ├── render/      # OpenGL 渲染（CCOpenGLWidget + GLSL shaders）
│   │   ├── network/     # 网络通信（CCVideoClient, WebSocketControlClient）
│   │   └── resources/   # UI 资源（.ui, .qrc）
│   └── include/
│
├── ClientToMp4/         # RTMP 流转 MP4 录制工具
│   ├── src/
│   │   ├── core/        # 程序入口（main.cpp）
│   │   ├── network/     # TCP/RTMP 接收（CCVideoClient, FFmpegRTMPReceiver）
│   │   ├── writer/      # MP4 写入（CCVideoWriter）
│   │   └── thread/      # 线程工具
│   └── include/
│
└── docs/               # 设计文档
    ├── CCStreamServer/
    │   └── thread-pool-design.md
    ├── ClientToMp4/
    │   ├── ClientToMp4运行流程.md
    │   ├── RTMP流写成MP4实现流程.md
    │   ├── 多客户端实例文件冲突说明.md
    │   └── MP4单例模式说明.md
    └── QtClient/
        ├── QtClient运行流程.md
        └── QtClient_OpenGL流程.md
```

## 模块说明

### CCStreamServer（流媒体服务器）

负责从采集设备获取音视频数据，编码后通过 RTMP 推流到远程服务器。

**核心流程：** 采集 → 编码 → 美颜滤镜 → RTMP 发送

- **采集层**：H264Capture（视频）、ALSAAudioCapture（ALSA 音频）
- **编码层**：H264Encoder / HWVideoEncoder（视频）、AACEncoder（音频）
- **传输层**：FFmpegRTMPSender（RTMP 推送）
- **控制层**：WebSocketControlServer（远程控制接口）
- 使用**线程池**管理客户端连接，避免为每个客户端创建独立线程

### QtClient（Qt 桌面客户端）

接收服务器音视频流，解码后在 OpenGL 中渲染显示。

**核心流程：** 接收 → 解码 → OpenGL 渲染

- H.264 / AAC 解码后得到 YUV420P 数据
- 通过 **GLSL 片段着色器**在 GPU 中完成 YUV→RGB 转换并渲染
- 支持 WebSocket 远程控制

### ClientToMp4（录制工具）

支持两种录制模式：

- **TCP 模式**：从 CCStreamServer 通过 TCP 连接接收音视频流，写入 MP4
- **RTMP 模式**：从任意 RTMP 服务器拉流，写入 MP4

使用 FFmpeg 的 libavformat 完成 MP4 封装（H.264 + AAC）。

## 技术栈

| 组件 | 技术 |
|------|------|
| 音视频编解码 | FFmpeg (libavformat, libavcodec, libswscale) |
| 视频编码 | x264 / 硬件编码 (VideoToolbox) |
| 流媒体协议 | RTMP (librtmp) |
| 音频采集 | ALSA (Linux) |
| GUI 渲染 | Qt6 + OpenGL + GLSL |
| 网络通信 | TCP Socket, WebSocket (libwebsockets) |
| 构建系统 | CMake (CCStreamServer, ClientToMp4), qmake (QtClient) |

## 依赖

### Linux / macOS

```bash
# Ubuntu/Debian
sudo apt install libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev libx264-dev \
    libasound2-dev libwebsockets-dev pkg-config cmake
```

### Qt6

```bash
# macOS
brew install qt6
```

## 构建

### CCStreamServer

```bash
cd CCStreamServer
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### QtClient

```bash
cd QtClient
qmake QtVideoClient.pro
make -j$(nproc)
# 或使用 Qt Creator 打开 .pro 文件
```

### ClientToMp4

```bash
cd ClientToMp4
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 使用

### CCStreamServer

```bash
./CCStreamServer
# 默认监听端口 30000
```

### QtClient

```bash
./QtClient
# 连接服务器 IP:30000
```

### ClientToMp4 (TCP 模式)

```bash
./CCVideoClient tcp 127.0.0.1 30000
```

### ClientToMp4 (RTMP 模式)

```bash
./CCVideoClient rtmp rtmp://example.com/live/stream
```

## 公共类型统一（common/）

本项目将跨模块共享的类型定义统一放在 `common/include/`，避免重复定义导致的不一致。

**关键修复：** 原始代码中 `CC_MsgHeader` 在不同模块间定义不一致（ClientToMp4 缺少 `timestamp` 字段），现已统一修复。

旧的头文件（如 `CCSocketDefine.h`）保留为 deprecated wrapper，通过 `#include` 指向 `common/` 中的统一版本，向后兼容现有代码。

## 许可证

见 [LICENSE](./LICENSE) 文件。

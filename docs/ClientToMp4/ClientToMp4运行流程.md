# ClientToMp4 项目运行流程文档

## 项目概述

ClientToMp4 是一个智能直播录制客户端，支持两种录制模式：
1. **TCP模式**：从流媒体服务器通过TCP连接接收H.264视频流和AAC音频流，并录制为MP4文件
2. **RTMP模式**：从RTMP服务器拉取视频流，并录制为MP4文件

## 技术架构

### 核心组件

1. **main.cpp** - 程序入口，负责参数解析和模式选择
2. **CCVideoClient** - TCP客户端，负责TCP连接和数据接收
3. **CCVideoWriter** - MP4写入器（单例模式），负责将音视频数据写入MP4文件
4. **FFmpegRTMPReceiver** - RTMP拉流器，负责从RTMP服务器拉取视频流
5. **CCThread** - 线程工具类，提供线程创建功能

### 依赖库

- **FFmpeg** - 用于MP4文件封装和RTMP拉流
- **pthread** - 多线程支持
- **faac** - AAC音频编码（可选）

---

## 运行流程详解

### 一、程序启动流程

```
main() 函数启动
    ↓
解析命令行参数
    ↓
判断模式类型
    ├─→ TCP模式 → 执行TCP录制流程
    └─→ RTMP模式 → 执行RTMP录制流程
```

### 二、TCP模式运行流程

#### 2.1 初始化阶段

```
main() 
    ↓
创建 CCVideoClient 对象
    ↓
调用 StartSocketConnection()
    ├─→ 创建TCP Socket (socket)
    ├─→ 配置服务器地址 (sockaddr_in)
    ├─→ 设置非阻塞模式 (fcntl)
    ├─→ 发起连接 (connect)
    ├─→ 创建等待连接线程 (doWaitConnectionThread)
    ├─→ 创建接收流线程 (startRecvStreamThread)
    ├─→ 启动MP4录制 (CCVideoWriter::StartVideoWriterWithPath)
    └─→ 发送心跳包 (sendKeepAlivePacket)
```

#### 2.2 连接建立阶段

```
等待连接线程 (RunWaitConnection)
    ↓
使用 select() 检测socket状态
    ↓
检查连接是否成功 (getsockopt SO_ERROR)
    ↓
设置连接标志 m_bConnected = true
```

#### 2.3 数据接收阶段

```
接收流线程 (RunRecvAVStream)
    ↓
循环接收数据包
    ├─→ 接收消息头 (CC_MsgHeader)
    ├─→ 验证消息头 (headerID == "CCTC")
    ├─→ 判断消息类型
    │   ├─→ MSGHEADER_TYPE_AVSTREAM (音视频流)
    │   │   ├─→ CONTENT_AVSTREAM_VIDEO → 写入视频数据
    │   │   └─→ CONTENT_AVSTREAM_AUDIO → 写入音频数据
    │   └─→ MSGHEADER_TYPE_KEEPALIVE (心跳包)
    └─→ 继续接收下一个数据包
```

#### 2.4 MP4写入阶段

```
CCVideoWriter::WriteVideoData()
    ↓
解析H.264 NALU单元 (readOneNaluFromBuff)
    ├─→ SPS (序列参数集) → 保存到 m_avcCBox
    ├─→ PPS (图像参数集) → 保存到 m_avcCBox
    ├─→ IDR帧 (关键帧) → 转换为AVCC格式并写入
    └─→ P帧 (普通帧) → 转换为AVCC格式并写入
        ↓
    writeVideoFrame()
        ├─→ 初始化视频写入器 (initVideoWriter)
        │   ├─→ 分配格式上下文 (avformat_alloc_context)
        │   ├─→ 猜测输出格式 (av_guess_format: "mov")
        │   ├─→ 初始化视频流 (initVideoStreamInfo)
        │   ├─→ 初始化音频流 (initAudioStreamInfo)
        │   └─→ 写入文件头 (writeVideoHeader)
        ├─→ 创建AVPacket
        ├─→ 设置时间戳 (PTS/DTS)
        └─→ 写入帧数据 (av_interleaved_write_frame)
```

#### 2.5 心跳保持阶段

```
心跳线程 (sendKeepAlivePacket)
    ↓
每2秒发送一次心跳包
    ├─→ 构造心跳消息头 (MSGHEADER_TYPE_KEEPALIVE)
    └─→ 发送到服务器 (sendSocketData)
```

#### 2.6 程序退出阶段

```
收到 SIGINT 信号 (Ctrl+C)
    ↓
设置 m_bThreadRuning = false
    ↓
析构函数执行
    ├─→ 停止MP4写入 (StopWriteReleaseResources)
    │   ├─→ 写入文件尾 (av_write_trailer)
    │   ├─→ 关闭编码器 (avcodec_close)
    │   ├─→ 关闭文件 (avio_close)
    │   └─→ 释放格式上下文 (avformat_free_context)
    └─→ 关闭Socket连接 (closeSocketConnection)
```

### 三、RTMP模式运行流程

#### 3.1 初始化阶段

```
main()
    ↓
创建 FFmpegRTMPReceiver 对象
    ↓
设置回调函数
    ├─→ H.264数据回调 (h264Callback)
    └─→ AAC数据回调 (aacCallback)
    ↓
调用 StartReceive()
    ├─→ 分配格式上下文 (avformat_alloc_context)
    ├─→ 设置超时选项 (AVDictionary)
    ├─→ 打开输入流 (avformat_open_input)
    ├─→ 查找流信息 (avformat_find_stream_info)
    ├─→ 查找视频流索引
    ├─→ 查找音频流索引
    ├─→ 获取视频信息 (分辨率、帧率)
    └─→ 启动拉流线程 (ReceiveThreadFunc)
```

#### 3.2 拉流阶段

```
拉流线程 (ReceiveThreadFunc)
    ↓
循环读取数据包 (av_read_frame)
    ├─→ 视频数据包
    │   └─→ ProcessVideoPacket()
    │       ├─→ 判断是否为关键帧
    │       ├─→ 计算时间戳 (PTS)
    │       └─→ ExtractNALUnits()
    │           ├─→ 解析AVCC格式的NAL单元
    │           ├─→ 转换为Annex-B格式 (添加起始码)
    │           └─→ 回调H.264数据 (h264Callback)
    └─→ 音频数据包
        └─→ ProcessAudioPacket()
            ├─→ 计算时间戳 (PTS)
            └─→ 回调AAC数据 (aacCallback)
```

#### 3.3 MP4写入阶段

```
回调函数执行
    ├─→ h264Callback()
    │   └─→ CCVideoWriter::WriteVideoData()
    │       └─→ (同TCP模式的MP4写入流程)
    └─→ aacCallback()
        └─→ CCVideoWriter::WriteAudioData()
            └─→ writeAudioStream()
                ├─→ 创建AVPacket
                ├─→ 设置时间戳
                └─→ 写入帧数据 (av_interleaved_write_frame)
```

#### 3.4 程序退出阶段

```
main() 检测到连接断开
    ↓
调用 StopReceive()
    ├─→ 设置停止标志
    ├─→ 等待线程结束 (pthread_join)
    ├─→ 释放解码器上下文
    └─→ 关闭输入流 (avformat_close_input)
    ↓
调用 StopWriteReleaseResources()
    └─→ (同TCP模式的资源释放流程)
```

---

## 关键数据结构

### CC_MsgHeader (TCP消息头)
```cpp
struct CC_MsgHeader {
    char headerID[4];      // 消息标识 "CCTC"
    uint16_t msgType;      // 消息类型 (心跳/音视频流)
    uint16_t subType;      // 子类型 (视频/音频)
    uint16_t length;       // 数据长度
}
```

### H264_NaluUnit (H.264 NALU单元)
```cpp
struct H264_NaluUnit {
    int type;              // NALU类型 (SPS/PPS/IDR/SLICE等)
    int size;              // 数据大小
    unsigned char* data;   // 数据指针
}
```

### H264_RecordAvccBox (AVCC配置盒)
```cpp
struct H264_RecordAvccBox {
    int sps_length;        // SPS长度
    int pps_length;        // PPS长度
    unsigned char spsBuffer[128];  // SPS数据
    unsigned char ppsBuffer[64];   // PPS数据
}
```

---

## 关键技术点

### 1. 非阻塞TCP连接
- 使用 `fcntl()` 设置 `O_NONBLOCK` 标志
- 使用 `select()` 检测连接完成
- 使用 `getsockopt()` 检查连接状态

### 2. H.264格式转换
- **Annex-B格式**：使用起始码 `0x00 0x00 0x00 0x01` 分隔NALU
- **AVCC格式**：使用4字节长度前缀分隔NALU（MP4容器格式）
- 转换过程：从Annex-B解析NALU，转换为AVCC格式写入MP4

### 3. MP4文件结构
- **ftyp box**：文件类型标识
- **moov box**：元数据（视频/音频参数、时间戳等）
- **mdat box**：媒体数据（实际的音视频帧）

### 4. 时间戳管理
- 使用 `std::chrono` 记录开始时间
- 计算相对时间戳（PTS/DTS）
- 使用 `av_rescale_q()` 转换时间基

### 5. 线程安全
- 使用 `pthread_mutex` 保护Socket操作
- 使用 `std::mutex` 保护MP4写入操作
- 单例模式使用双重检查锁定

---

## 使用示例

### TCP模式
```bash
./ClientToMp4 tcp 192.168.236.101 30000
```

### RTMP模式
```bash
./ClientToMp4 rtmp rtmp://live.bilibili.com/app/stream
```

---

## 文件说明

| 文件 | 功能 |
|------|------|
| main.cpp | 程序入口，参数解析，模式选择 |
| CCVideoClient.cpp | TCP客户端实现，数据接收 |
| CCVideoWriter.cpp | MP4写入器实现，文件封装 |
| FFmpegRTMPReceiver.cpp | RTMP拉流器实现 |
| CCThread.cpp | 线程工具函数 |
| CCSocketDefine.h | Socket相关定义和数据结构 |

---

## 注意事项

1. **TCP模式**需要服务器按照协议发送数据包（消息头 + 数据体）
2. **RTMP模式**需要RTMP服务器支持拉流
3. MP4文件在程序退出时才会写入完整的文件尾，确保文件可播放
4. 首次写入视频帧时会初始化MP4文件（需要SPS/PPS信息）
5. 心跳包用于保持TCP连接活跃，防止服务器断开连接

---

## 错误处理

- Socket连接失败：检查服务器IP和端口
- RTMP连接失败：检查RTMP URL和网络连接
- MP4写入失败：检查文件路径权限和磁盘空间
- 数据解析失败：检查数据格式是否符合协议

---

## 性能优化建议

1. 使用缓冲区减少系统调用
2. 异步写入MP4文件（当前为同步）
3. 支持多文件分段录制
4. 添加错误重试机制
5. 优化内存分配（使用内存池）


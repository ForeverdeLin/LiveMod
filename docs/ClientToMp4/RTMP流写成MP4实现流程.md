# RTMP流写成MP4文件实现流程

## 概述

本文档描述从RTMP服务器拉取视频流并录制为MP4文件的完整实现流程，基于FFmpeg库实现。

## 整体架构

```
RTMP服务器 → FFmpegRTMPReceiver → 回调函数 → CCVideoWriter → MP4文件
```

## 实现流程

### 一、RTMP拉流初始化阶段

#### 1.1 创建RTMP接收器
- 创建 `FFmpegRTMPReceiver` 对象
- 设置H.264数据回调和AAC数据回调函数

#### 1.2 初始化FFmpeg输入流
**关键API：**
- `avformat_alloc_context()` - 分配格式上下文
- `av_dict_set()` - 设置RTMP连接超时选项（rtmp_timeout、stimeout）
- `avformat_open_input()` - 打开RTMP输入流，连接到RTMP服务器
- `avformat_find_stream_info()` - 查找流信息，获取视频/音频参数

#### 1.3 查找音视频流
- 遍历所有流，查找视频流（`codec_type == AVMEDIA_TYPE_VIDEO`）
- 获取视频信息：分辨率（width/height）、帧率（avg_frame_rate）
- 查找音频流（`codec_type == AVMEDIA_TYPE_AUDIO`，可选）

#### 1.4 启动拉流线程
- 创建独立线程执行 `ReceiveThreadFunc()` 持续拉流

---

### 二、RTMP拉流阶段

#### 2.1 循环读取数据包
**关键API：**
- `av_packet_alloc()` - 分配AVPacket
- `av_read_frame()` - 从RTMP流中读取一个数据包
- 根据 `pkt->stream_index` 判断是视频包还是音频包

#### 2.2 处理视频数据包
**流程：**
1. 判断是否为关键帧（`pkt->flags & AV_PKT_FLAG_KEY`）
2. 计算时间戳（PTS）：
   - `av_rescale_q()` - 将时间戳从流的时间基转换为毫秒
3. 提取NAL单元：
   - RTMP流中的H.264数据是AVCC格式（4字节长度前缀）
   - 解析每个NAL单元，添加Annex-B起始码（0x00 0x00 0x00 0x01）
   - 通过回调函数传递H.264数据

#### 2.3 处理音频数据包
**流程：**
1. 计算时间戳（PTS）
2. 通过回调函数传递AAC数据

---

### 三、MP4文件初始化阶段

#### 3.1 启动MP4写入器
**关键API：**
- `CCVideoWriter::GetInstance()` - 获取单例实例
- `CCVideoWriter::StartVideoWriterWithPath()` - 设置MP4文件路径
- `CCVideoWriter::SetVideoSize()` - 设置视频分辨率

#### 3.2 初始化MP4格式上下文
**关键API：**
- `avformat_alloc_context()` - 分配输出格式上下文
- `av_guess_format("mov", NULL, NULL)` - 猜测MP4格式（MP4基于QuickTime MOV格式）

#### 3.3 创建视频流
**关键API：**
- `avformat_new_stream()` - 创建新的视频流
- 配置编码器参数：
  - `codec_id = AV_CODEC_ID_H264`
  - `codec_type = AVMEDIA_TYPE_VIDEO`
  - `width/height` - 视频分辨率
  - `time_base` - 时间基（1/帧率）
  - `extradata` - AVCC配置（包含SPS/PPS，等待首次I帧时设置）

#### 3.4 创建音频流
**关键API：**
- `avformat_new_stream()` - 创建新的音频流
- 配置编码器参数：
  - `codec_id = AV_CODEC_ID_AAC`
  - `codec_type = AVMEDIA_TYPE_AUDIO`
  - `sample_rate` - 采样率（如8000Hz）
  - `channels` - 声道数（如1=单声道）
  - `extradata` - AAC配置头（2字节）

#### 3.5 写入MP4文件头
**关键API：**
- `avio_open()` - 打开输出文件
- `avformat_write_header()` - 写入MP4文件头（ftyp、moov等box）

---

### 四、MP4数据写入阶段

#### 4.1 视频数据写入流程

**4.1.1 接收H.264数据**
- 回调函数接收到Annex-B格式的H.264数据
- 调用 `CCVideoWriter::WriteVideoData()`

**4.1.2 解析NALU单元**
- `readOneNaluFromBuff()` - 从Annex-B格式数据中提取NALU单元
- 识别NALU类型：
  - **SPS**（`type == H264NT_SPS`）：保存到 `m_avcCBox.spsBuffer`
  - **PPS**（`type == H264NT_PPS`）：保存到 `m_avcCBox.ppsBuffer`
  - **IDR帧**（`type == H264NT_SLICE_IDR`）：关键帧
  - **P帧**（`type == H264NT_SLICE`）：普通帧

**4.1.3 格式转换（Annex-B → AVCC）**
- MP4容器使用AVCC格式（4字节长度前缀）
- 转换过程：
  1. 移除Annex-B起始码
  2. 添加4字节长度前缀（大端序）
  3. 构造AVCC格式的帧数据

**4.1.4 写入视频帧**
**关键API：**
- `av_init_packet()` - 初始化AVPacket
- `av_rescale_q()` - 转换时间戳到流的时间基
- `av_interleaved_write_frame()` - 写入视频帧到MP4文件

**时间戳处理：**
- 使用相对时间戳（从开始录制的时间点计算）
- 计算PTS/DTS（显示时间戳/解码时间戳）
- 设置关键帧标志（`AV_PKT_FLAG_KEY`）

#### 4.2 音频数据写入流程

**4.2.1 接收AAC数据**
- 回调函数接收到AAC音频数据
- 调用 `CCVideoWriter::WriteAudioData()`

**4.2.2 写入音频帧**
**关键API：**
- `av_init_packet()` - 初始化AVPacket
- `av_rescale_q()` - 转换时间戳
- `av_interleaved_write_frame()` - 写入音频帧到MP4文件

**注意：**
- 音频帧的时间戳需要与视频帧同步
- 使用互斥锁保护写入操作（线程安全）

---

### 五、资源释放阶段

#### 5.1 停止RTMP拉流
**关键API：**
- 设置停止标志 `m_bReceiving = false`
- `pthread_join()` - 等待拉流线程结束
- `avcodec_free_context()` - 释放解码器上下文
- `avformat_close_input()` - 关闭输入流

#### 5.2 完成MP4文件写入
**关键API：**
- `av_write_trailer()` - 写入MP4文件尾（moov box最终位置等）
- `avcodec_close()` - 关闭编码器上下文
- `avio_close()` - 关闭文件IO
- `avformat_free_context()` - 释放格式上下文

---

## 关键技术点

### 1. H.264格式转换
- **Annex-B格式**：使用起始码 `0x00 0x00 0x00 0x01` 分隔NALU（RTMP接收到的格式）
- **AVCC格式**：使用4字节长度前缀分隔NALU（MP4容器格式）
- 转换时机：从RTMP接收时转换为Annex-B，写入MP4时转换为AVCC

### 2. SPS/PPS处理
- SPS（序列参数集）和PPS（图像参数集）是H.264解码必需的参数
- 首次收到SPS/PPS时保存，用于构造AVCC配置头
- AVCC配置头写入到 `codec->extradata`，供MP4文件头使用

### 3. 时间戳管理

#### 3.1 H.264编码层 vs 传输层/容器层时间戳

**重要概念：H.264编码层本身不包含时间戳信息！**

- **H.264编码层**：只包含编码数据（NALU单元），没有时间戳
- **传输层时间戳**：时间戳由传输协议提供
  - **TCP模式**：在应用层协议的消息头中传递时间戳（`CC_MsgHeader.timestamp`）
  - **RTMP模式**：在RTMP协议层提供时间戳（`AVPacket.pts`）
- **容器层时间戳**：MP4容器格式使用自己的时间戳系统

#### 3.2 时间戳流转过程

**RTMP模式：**
```
RTMP流中的时间戳（RTMP协议层）
    ↓
FFmpeg从RTMP读取 → AVPacket.pts（流的时间基）
    ↓
av_rescale_q() 转换 → 毫秒时间戳（统一时间基）
    ↓
回调函数传递 → 相对时间戳（从录制开始计算）
    ↓
MP4写入时 → av_rescale_q() 转换 → MP4流的时间基
    ↓
写入AVPacket.pts/dts → MP4容器时间戳
```

**TCP模式（当前实现）：**
```
服务端计算时间戳（应用层协议）
    ↓
CC_MsgHeader.timestamp（毫秒）
    ↓
TCP发送（消息头中包含时间戳）
    ↓
客户端接收（但当前实现未使用msgHeader.timestamp）
    ↓
客户端自己计算相对时间戳（从录制开始）
    ↓
MP4写入时 → av_rescale_q() 转换 → MP4流的时间基
    ↓
写入AVPacket.pts/dts → MP4容器时间戳
```

**注意：** TCP模式中，虽然服务端在消息头中发送了时间戳，但客户端当前实现没有使用，而是自己计算相对时间戳。理论上可以使用 `msgHeader.timestamp` 来保持更准确的时间同步。

#### 3.3 时间基（Time Base）转换

**RTMP流的时间基：**
- 从 `stream->time_base` 获取（如 1/1000 或 1/90000）
- 通过 `av_rescale_q(pkt->pts, stream->time_base, {1, 1000})` 转换为毫秒

**MP4流的时间基：**
- 视频流：`time_base = {1, frame_rate}`（如 1/25 表示25fps）
- 音频流：`time_base = {1, 1000}`（毫秒）
- 通过 `av_rescale_q(毫秒时间戳, {1, 1000}, stream->time_base)` 转换

#### 3.4 PTS和DTS的区别

- **PTS（Presentation Time Stamp）**：显示时间戳，帧应该显示的时间
- **DTS（Decode Time Stamp）**：解码时间戳，帧应该解码的时间
- **B帧场景**：如果有B帧，DTS < PTS（需要先解码后显示）
- **本项目**：代码中设置 `packet.dts = packet.pts`（无B帧，解码和显示时间相同）

#### 3.5 代码中的时间戳处理

**RTMP接收阶段**（`FFmpegRTMPReceiver::ProcessVideoPacket`）：
```cpp
// 从RTMP流的时间基转换为毫秒
pts = av_rescale_q(pkt->pts, stream->time_base, (AVRational){1, 1000});
```

**TCP接收阶段**（`CCVideoClient::RunRecvAVStream`）：
```cpp
// 接收消息头（包含timestamp字段，但当前实现未使用）
CC_MsgHeader msgHeader;
recvSocketData((char*)&msgHeader, sizeof(CC_MsgHeader));
// msgHeader.timestamp 包含服务端发送的时间戳（毫秒）
// 但当前代码没有使用，而是自己计算相对时间戳
```

**服务端TCP发送阶段**（`CCServerController::SendAVStream`）：
```cpp
// 计算视频PTS（基于帧计数和帧率）
int64_t videoPts = (frameCount * 1000) / m_pCamera->encoder.param->i_fps_num;
// 发送到TCP客户端（时间戳在消息头中）
m_pTCPServer->SendAVStream(..., videoPts);
```

**MP4写入阶段**（`CCVideoWriter::writeVideoFrame`）：
```cpp
// 从毫秒转换为MP4流的时间基
int curPts = av_rescale_q(m_stFrameInfo.m_nTotalTime, time_base, m_pVideoStream->time_base);
packet.pts = packet.dts = curPts;
```

#### 3.6 关键要点

1. **H.264编码数据本身没有时间戳**，时间戳来自传输层或容器层
2. **TCP模式**：时间戳在应用层协议的消息头中传递（`CC_MsgHeader.timestamp`），但客户端当前实现未使用
3. **RTMP模式**：时间戳在RTMP协议层提供（`AVPacket.pts`），通过FFmpeg获取
4. **时间基必须转换**：不同层使用不同的时间基，必须用 `av_rescale_q()` 转换
5. **相对时间戳**：使用从录制开始计算的相对时间，而不是绝对时间
6. **音视频同步**：视频和音频使用统一的时间基准，确保同步播放

#### 3.7 TCP模式时间戳说明

**服务端发送：**
- 在 `CC_MsgHeader` 消息头中包含 `timestamp` 字段（毫秒）
- 服务端计算：`videoPts = (frameCount * 1000) / fps`
- 时间戳随消息头一起发送到客户端

**客户端接收（当前实现）：**
- 虽然接收了 `msgHeader.timestamp`，但**没有使用**
- 客户端自己计算相对时间戳（从录制开始）
- 理论上可以使用服务端发送的时间戳，保持更准确的时间同步

**改进建议：**
- 客户端可以使用 `msgHeader.timestamp` 作为时间戳
- 这样可以保持服务端和客户端的时间戳一致
- 对于多客户端场景，使用统一的时间戳更有利于同步

### 4. 线程安全
- RTMP拉流在独立线程中执行
- MP4写入使用互斥锁保护（`std::mutex`）
- 单例模式使用双重检查锁定

### 5. 关键帧处理
- 必须等待收到第一个I帧（IDR帧）才开始写入视频数据
- I帧包含完整的图像信息，P帧依赖I帧解码

---

## 关键API总结

### RTMP拉流相关
- `avformat_alloc_context()` - 分配格式上下文
- `avformat_open_input()` - 打开输入流
- `avformat_find_stream_info()` - 查找流信息
- `av_read_frame()` - 读取数据包
- `avformat_close_input()` - 关闭输入流

### MP4写入相关
- `avformat_alloc_context()` - 分配输出格式上下文
- `av_guess_format()` - 猜测输出格式
- `avformat_new_stream()` - 创建流
- `avio_open()` - 打开输出文件
- `avformat_write_header()` - 写入文件头
- `av_interleaved_write_frame()` - 写入帧数据
- `av_write_trailer()` - 写入文件尾
- `avio_close()` - 关闭文件

### 时间戳处理
- `av_rescale_q()` - 时间基转换
- `av_gettime()` - 获取当前时间

### 数据包处理
- `av_packet_alloc()` - 分配数据包
- `av_init_packet()` - 初始化数据包
- `av_packet_unref()` - 释放数据包引用
- `av_packet_free()` - 释放数据包

---

## 数据流向图

```
RTMP服务器
    ↓
avformat_open_input() 连接
    ↓
av_read_frame() 读取数据包
    ↓
ProcessVideoPacket() / ProcessAudioPacket()
    ↓
ExtractNALUnits() (视频) / 直接传递 (音频)
    ↓
回调函数 (h264Callback / aacCallback)
    ↓
CCVideoWriter::WriteVideoData() / WriteAudioData()
    ↓
解析NALU (readOneNaluFromBuff)
    ↓
格式转换 (Annex-B → AVCC)
    ↓
writeVideoFrame() / writeAudioStream()
    ↓
av_interleaved_write_frame() 写入MP4
    ↓
MP4文件
```

---

## 注意事项

1. **首次I帧等待**：必须等待收到第一个I帧（包含SPS/PPS）才能开始写入视频数据
2. **时间戳同步**：确保视频和音频的时间戳同步，避免音画不同步
3. **错误处理**：RTMP连接断开时需要重连或退出
4. **资源释放**：程序退出时必须调用 `av_write_trailer()` 完成MP4文件，否则文件无法播放
5. **线程安全**：多线程环境下使用互斥锁保护共享资源
6. **内存管理**：及时释放AVPacket和临时缓冲区，避免内存泄漏

## RTMP模式 vs TCP模式的文件写入

### 单例模式的使用

**重要：** RTMP模式和TCP模式使用**同一个单例实例**（`CCVideoWriter::GetInstance()`），但**不会同时运行**。

### 运行模式

从 `main.cpp` 代码可以看到，两种模式是**互斥的**（if-else结构）：

```cpp
if (strcmp(mode, "tcp") == 0) {
    // TCP模式
    CCVideoClient videoClient;
    videoClient.StartSocketConnection(&networkInfo);
    // TCP模式内部调用：StartVideoWriterWithPath("./20211115.mp4")
}
else if (strcmp(mode, "rtmp") == 0) {
    // RTMP模式
    FFmpegRTMPReceiver receiver;
    // RTMP模式调用：StartVideoWriterWithPath("record_YYYYMMDD_HHMMSS.mp4")
}
```

### 文件路径

- **TCP模式**：写入固定文件 `./20211115.mp4`（在`CCVideoClient::StartSocketConnection()`中设置）
- **RTMP模式**：写入动态文件名 `record_YYYYMMDD_HHMMSS.mp4`（在`main.cpp`中生成）

### 关键点

1. **不会同时运行**：通过命令行参数选择模式，一次只能运行一种模式
2. **共享单例实例**：两种模式都使用 `CCVideoWriter::GetInstance()`，但不会同时访问
3. **不同的文件**：每种模式写入不同的文件（文件名不同）
4. **资源管理**：单例模式确保FFmpeg资源统一管理，避免重复分配

### 如果同时运行会怎样？

**理论上**（虽然代码中不会发生）：
- 如果两个模式同时运行，后调用的 `StartVideoWriterWithPath()` 会覆盖文件路径
- `writeVideoHeader()` 中的 `avio_open()` 会打开新文件，可能导致之前的文件未正确关闭
- 数据可能写入错误的文件，导致文件损坏

**实际使用**：
- 由于是互斥选择，不会出现这种情况
- 每次运行只选择一种模式，写入一个MP4文件

## 多客户端实例文件冲突问题

### 单例模式的作用范围

**重要：单例模式只在单个进程内有效！**

- **单进程内**：单例模式确保只有一个实例
- **多进程间**：每个进程都有自己独立的单例实例

### 文件冲突情况

| 场景 | TCP模式 | RTMP模式 | 是否冲突 |
|------|---------|----------|---------|
| 多个TCP客户端 | `./20211115.mp4`（硬编码） | - | ❌ **冲突** |
| 多个RTMP客户端（不同时间） | - | 不同文件名 | ✅ **不冲突** |
| 多个RTMP客户端（同一秒） | - | 相同文件名 | ❌ **冲突** |
| TCP + RTMP | `./20211115.mp4` | `record_*.mp4` | ✅ **不冲突** |

### 冲突原因

1. **TCP模式**：使用硬编码文件名 `./20211115.mp4`，所有TCP客户端实例都会写入同一个文件
2. **RTMP模式**：使用动态文件名 `record_YYYYMMDD_HHMMSS.mp4`，但只精确到秒级，同一秒内启动的多个实例会冲突

### 文件冲突的影响

- **文件头覆盖**：多个进程写入文件头，导致文件结构损坏
- **数据交错写入**：视频帧和音频帧可能写入错误位置
- **文件尾不完整**：文件尾被覆盖，导致文件无法播放

### 解决方案

**推荐：添加进程ID到文件名**
```cpp
// TCP模式改进
pid_t pid = getpid();
snprintf(filename, sizeof(filename), "./record_tcp_%d_%ld.mp4", pid, time(nullptr));

// RTMP模式改进
snprintf(filename, sizeof(filename), 
         "record_%04d%02d%02d_%02d%02d%02d_%d.mp4",
         year, month, day, hour, min, sec, pid);
```

详细说明请参考：`多客户端实例文件冲突说明.md`


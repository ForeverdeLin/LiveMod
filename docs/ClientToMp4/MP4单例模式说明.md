# CCVideoWriter 单例模式说明

## 一、单例模式实现

### 1.1 实现方式

`CCVideoWriter` 使用**线程安全的单例模式**，采用**双重检查锁定（Double-Checked Locking）**实现。

**关键代码：**

```cpp
// 静态成员变量
static CCVideoWriter* m_pInstance = NULL;  // 单例实例指针
static std::mutex m_mutex;                 // 互斥锁

// 获取单例实例
CCVideoWriter* CCVideoWriter::GetInstance()
{
    // 第一次检查：避免不必要的加锁
    if(m_pInstance == nullptr)
    {
        // 加锁：防止多线程同时创建实例
        m_mutex.lock();
        // 第二次检查：再次确认实例不存在
        if(m_pInstance == nullptr)
        {
            m_pInstance = new CCVideoWriter();  // 创建唯一实例
        }
        m_mutex.unlock();
    }
    return m_pInstance;
}
```

### 1.2 构造函数私有化

```cpp
private:
    CCVideoWriter();  // 构造函数私有，防止外部直接创建
```

### 1.3 自动资源释放

使用**Garbage类**在程序退出时自动释放单例实例：

```cpp
class Garbage {
public:
    ~Garbage() {
        if(CCVideoWriter::m_pInstance != nullptr) {
            delete CCVideoWriter::m_pInstance;
            CCVideoWriter::m_pInstance = nullptr;
        }
    }
};

static Garbage m_garbage;  // 静态对象，程序退出时自动析构
```

---

## 二、单例模式的具体作用

### 2.1 全局唯一实例

**作用：** 确保整个程序中只有一个 `CCVideoWriter` 实例。

**应用场景：**
- 多个线程同时写入MP4文件（视频线程、音频线程）
- 多个回调函数需要访问同一个MP4写入器
- TCP模式和RTMP模式可能同时使用

**代码示例：**
```cpp
// RTMP模式：回调函数中使用
auto h264Callback = [](uint8_t* data, int size, bool isKeyFrame, int64_t pts) {
    CCVideoWriter::GetInstance()->WriteVideoData(data, size);  // 全局唯一实例
};

// TCP模式：接收线程中使用
CCVideoWriter::GetInstance()->WriteVideoData(sBuffer, msgHeader.length);  // 同一个实例
```

### 2.2 资源统一管理

**作用：** 统一管理FFmpeg资源，避免重复分配和释放。

**管理的资源包括：**
- `AVFormatContext* m_pFormatContext` - MP4格式上下文
- `AVStream* m_pVideoStream` - 视频流
- `AVStream* m_pAudioStream` - 音频流
- `H264_RecordAvccBox m_avcCBox` - SPS/PPS配置
- `AACEncodeConfig* g_aacEncodeConfig` - AAC编码配置

**好处：**
- 避免多个实例重复分配FFmpeg资源
- 统一管理文件句柄，确保只有一个MP4文件被写入
- 简化资源释放逻辑

### 2.3 状态统一维护

**作用：** 维护统一的录制状态，确保音视频数据写入同一个MP4文件。

**维护的状态包括：**
- `m_bRecordStatus` - 录制状态标志
- `m_bStartRecordStatus` - 开始录制标志（等待I帧）
- `m_stFrameInfo` - 视频帧信息（时间戳、PTS等）
- `m_stAudioInfo` - 音频帧信息（时间戳、PTS等）
- `m_startTimeStamp` - 录制开始时间戳

**好处：**
- 确保视频和音频写入同一个文件
- 统一的时间戳管理，保证音视频同步
- 避免状态不一致导致的文件损坏

### 2.4 线程安全保护

**作用：** 在多线程环境下安全地写入MP4文件。

**线程安全机制：**
1. **单例创建时的线程安全**：使用双重检查锁定
2. **写入操作时的线程安全**：使用互斥锁保护

```cpp
// 写入视频帧时的线程保护
bool CCVideoWriter::writeVideoFrame(...)
{
    // ...
    m_vmMutex.lock();
    int ret = av_interleaved_write_frame(m_pFormatContext, &packet);
    m_vmMutex.unlock();
    // ...
}

// 写入音频帧时的线程保护
bool CCVideoWriter::writeAudioStream(...)
{
    // ...
    m_vMutex.lock();
    int ret = av_interleaved_write_frame(m_pFormatContext, &packet);
    m_vMutex.unlock();
    // ...
}
```

**应用场景：**
- RTMP拉流线程（接收视频/音频数据）
- TCP接收线程（接收视频/音频数据）
- 多个回调函数可能在不同线程中调用

### 2.5 简化调用接口

**作用：** 提供全局访问点，无需传递对象指针。

**使用方式对比：**

**不使用单例（需要传递对象）：**
```cpp
// 需要传递writer对象
void processVideo(CCVideoWriter* writer, uint8_t* data, int size) {
    writer->WriteVideoData(data, size);
}

// 回调函数需要捕获writer对象
auto callback = [writer](uint8_t* data, int size) {
    writer->WriteVideoData(data, size);
};
```

**使用单例（全局访问）：**
```cpp
// 直接全局访问，无需传递对象
auto callback = [](uint8_t* data, int size) {
    CCVideoWriter::GetInstance()->WriteVideoData(data, size);
};

// 任何地方都可以直接调用
CCVideoWriter::GetInstance()->WriteVideoData(data, size);
```

**好处：**
- 简化回调函数设计（Lambda函数无需捕获对象）
- 降低函数参数复杂度
- 便于在不同模块间共享

### 2.6 节省内存

**作用：** 避免创建多个实例，节省内存空间。

**内存占用：**
- 单个实例：约几KB到几十KB（取决于缓冲区大小）
- 多个实例：每个实例都占用相同的内存

**在项目中的体现：**
- TCP模式和RTMP模式可能同时运行，但共享同一个实例
- 多个回调函数共享同一个实例，而不是各自创建

---

## 三、单例模式在项目中的实际应用

### 3.1 RTMP模式中的应用

```cpp
// main.cpp - RTMP模式
int main(int argc, char* argv[])
{
    // 创建RTMP接收器
    FFmpegRTMPReceiver receiver;
    
    // 回调函数中使用单例（无需捕获对象）
    auto h264Callback = [](uint8_t* data, int size, bool isKeyFrame, int64_t pts) {
        CCVideoWriter::GetInstance()->WriteVideoData(data, size);
    };
    
    auto aacCallback = [](uint8_t* data, int size, int64_t pts) {
        CCVideoWriter::GetInstance()->WriteAudioData(data, size);
    };
    
    // 设置视频参数
    CCVideoWriter::GetInstance()->SetVideoSize(width, height);
    
    // 启动录制
    CCVideoWriter::GetInstance()->StartVideoWriterWithPath(filename);
    
    // 停止录制
    CCVideoWriter::GetInstance()->StopWriteReleaseResources();
}
```

### 3.2 TCP模式中的应用

```cpp
// CCVideoClient.cpp - TCP模式
void CCVideoClient::RunRecvAVStream()
{
    while(m_bThreadRuning)
    {
        // 接收消息头
        CC_MsgHeader msgHeader;
        recvSocketData((char*)&msgHeader, sizeof(CC_MsgHeader));
        
        if(msgHeader.subType == CONTENT_AVSTREAM_VIDEO)
        {
            // 直接使用单例，无需传递对象
            CCVideoWriter::GetInstance()->WriteVideoData(sBuffer, msgHeader.length);
        }
        else if(msgHeader.subType == CONTENT_AVSTREAM_AUDIO)
        {
            CCVideoWriter::GetInstance()->WriteAudioData(sBuffer, msgHeader.length);
        }
    }
}
```

### 3.3 多线程环境下的使用

**场景：** RTMP拉流线程和TCP接收线程可能同时运行

```cpp
// 线程1：RTMP拉流线程
void ReceiveThreadFunc() {
    // 接收视频数据
    CCVideoWriter::GetInstance()->WriteVideoData(data, size);  // 线程安全
}

// 线程2：TCP接收线程
void RunRecvAVStream() {
    // 接收视频数据
    CCVideoWriter::GetInstance()->WriteVideoData(data, size);  // 线程安全
}
```

**单例模式确保：**
- 两个线程使用同一个实例
- 互斥锁保护写入操作，避免数据竞争
- 状态统一管理，不会出现文件冲突

---

## 四、单例模式的优缺点

### 4.1 优点

1. **全局唯一访问点**：任何地方都可以访问，无需传递对象
2. **资源统一管理**：避免重复分配FFmpeg资源
3. **状态一致性**：确保音视频写入同一个文件
4. **线程安全**：通过互斥锁保护共享资源
5. **内存节省**：只创建一个实例
6. **简化设计**：回调函数无需捕获对象指针

### 4.2 缺点

1. **全局状态**：单例是全局状态，可能隐藏依赖关系
2. **测试困难**：难以进行单元测试（无法mock）
3. **扩展性限制**：无法同时写入多个MP4文件（需要修改设计）
4. **生命周期管理**：需要确保在程序退出时正确释放资源

### 4.3 改进建议

如果未来需要支持同时写入多个MP4文件，可以考虑：

1. **工厂模式**：创建多个 `CCVideoWriter` 实例
2. **对象池模式**：管理多个写入器实例
3. **策略模式**：根据需求选择单例或多实例

---

## 五、总结

`CCVideoWriter` 使用单例模式的主要目的是：

1. **确保全局唯一实例**：整个程序只有一个MP4写入器
2. **统一资源管理**：FFmpeg资源统一分配和释放
3. **状态一致性**：音视频数据写入同一个文件，时间戳统一管理
4. **线程安全**：多线程环境下安全写入
5. **简化接口**：提供全局访问点，无需传递对象

**适用场景：**
- 单文件录制（当前项目）
- 多线程写入同一个文件
- 需要全局访问的资源管理器

**不适用场景：**
- 需要同时写入多个MP4文件
- 需要为不同客户端创建独立的写入器
- 需要频繁创建和销毁写入器


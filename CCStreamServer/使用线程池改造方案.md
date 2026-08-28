# 使用线程池改造客户端处理方案

## 当前实现问题

**现状**：每个客户端创建2个线程（接收线程 + 发送线程）
- 100个客户端 = 200个线程
- 线程创建/销毁开销大
- 线程数量不可控，可能耗尽系统资源

## 线程池方案

**改进**：使用线程池管理客户端任务
- 固定数量的工作线程（如8-16个）
- 客户端任务放入队列，工作线程从队列取任务执行
- 线程复用，避免频繁创建/销毁

## 实现步骤

### 1. 创建线程池类（已完成）

- `ThreadPool.h` - 线程池类定义
- `ThreadPool.cpp` - 线程池实现

### 2. 改造CCServerController

在`CCServerController`中添加线程池成员：

```cpp
// CCServerController.h
#include "ThreadPool.h"

class CCServerController {
private:
    ThreadPool* m_pRecvThreadPool;    // 接收任务线程池
    ThreadPool* m_pSendThreadPool;    // 发送任务线程池
    // ... 其他成员
};
```

### 3. 初始化线程池

```cpp
// CCServerController.cpp
bool CCServerController::Initialize()
{
    // 创建线程池（每个线程池8个工作线程）
    m_pRecvThreadPool = new ThreadPool(8);   // 接收任务线程池
    m_pSendThreadPool = new ThreadPool(8);   // 发送任务线程池
    
    // ... 其他初始化
    return true;
}
```

### 4. 改造CCClientProcess

**方案A：将接收/发送逻辑封装成任务**

```cpp
// CCClientProcess.h
class CCClientProcess {
public:
    // 不再创建线程，而是提交任务到线程池
    void StartClientProcessWithSockfd(int sockfd, 
                                      ThreadPool* recvPool, 
                                      ThreadPool* sendPool);
    
    // 接收任务函数（供线程池调用）
    void RecvTask();
    
    // 发送任务函数（供线程池调用）
    void SendTask();
    
private:
    ThreadPool* m_pRecvPool;  // 接收线程池指针
    ThreadPool* m_pSendPool;  // 发送线程池指针
    std::atomic<bool> m_bRecvTaskRunning;  // 接收任务运行标志
    std::atomic<bool> m_bSendTaskRunning;  // 发送任务运行标志
};
```

```cpp
// CCClientProcess.cpp
void CCClientProcess::StartClientProcessWithSockfd(int sockfd, 
                                                    ThreadPool* recvPool, 
                                                    ThreadPool* sendPool)
{
    m_clientSockfd = sockfd;
    m_pRecvPool = recvPool;
    m_pSendPool = sendPool;
    m_bRecvTaskRunning = true;
    m_bSendTaskRunning = true;
    
    // 提交接收任务到线程池（循环提交，实现持续接收）
    SubmitRecvTask();
    
    // 提交发送任务到线程池（循环提交，实现持续发送）
    SubmitSendTask();
}

void CCClientProcess::SubmitRecvTask()
{
    if (!m_bRecvTaskRunning) {
        return;
    }
    
    // 将接收任务提交到线程池
    m_pRecvPool->EnqueueTask([this]() {
        RecvTask();
        
        // 如果还在运行，继续提交下一个接收任务
        if (m_bRecvTaskRunning) {
            SubmitRecvTask();
        }
    });
}

void CCClientProcess::RecvTask()
{
    // 原来的RunRecvProcess()逻辑
    int keepAliveCount = 5;
    
    // 非阻塞接收心跳包
    CC_MsgHeader msgHeader;
    memset((char*)&msgHeader, 0, sizeof(CC_MsgHeader));
    
    if (recvSocketData((char*)&msgHeader, sizeof(CC_MsgHeader))) {
        if (strcmp((char*)msgHeader.headerID, "CCTC") == 0) {
            if (msgHeader.msgType == MSGHEADER_TYPE_KEEPALIVE) {
                keepAliveCount = 5;  // 收到心跳，重置计数器
            }
        }
    } else {
        // 接收失败，检查是否超时
        keepAliveCount--;
        if (keepAliveCount <= 0) {
            disableClientRunningStatus();
            m_bRecvTaskRunning = false;
        }
    }
}

void CCClientProcess::SubmitSendTask()
{
    if (!m_bSendTaskRunning) {
        return;
    }
    
    // 将发送任务提交到线程池
    m_pSendThreadPool->EnqueueTask([this]() {
        SendTask();
        
        // 如果还在运行，继续提交下一个发送任务
        if (m_bSendTaskRunning) {
            SubmitSendTask();
        }
    });
}

void CCClientProcess::SendTask()
{
    // 原来的RunSendProcess()逻辑
    // 从队列中取数据发送
    CC_AVStream* stream = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        if (!m_avStreamQueue.empty()) {
            stream = m_avStreamQueue.front();
            m_avStreamQueue.pop();
        }
    }
    
    if (stream) {
        // 发送数据
        sendSocketData((char*)stream->data, stream->size);
        free(stream);
    }
}
```

### 5. 修改CCStreamServer使用线程池

```cpp
// CCStreamServer.cpp
void CCStreamServer::StartTCPServer()
{
    // ... 原有的socket初始化代码 ...
    
    while(m_bServerRunning)
    {
        int sockfd = accept(m_listenSockfd, (struct sockaddr*)&client_addr, &size);
        if(sockfd > 0)
        {
            printf("ACCEPT NEW CONNECTION: %d\n", sockfd);
            
            // 创建客户端处理对象
            CCClientProcess *clientProcess = new CCClientProcess();
            
            // 使用线程池启动客户端处理（传入线程池指针）
            clientProcess->StartClientProcessWithSockfd(
                sockfd, 
                m_pRecvThreadPool,    // 接收线程池
                m_pSendThreadPool    // 发送线程池
            );
            
            m_clients.push_back(clientProcess);
        }
        
        checkClientRunningStatus();
        usleep(2000*1000);
    }
}
```

## 方案B：更简单的实现（推荐）

如果不想大幅改动现有代码，可以只将接收/发送的循环逻辑改为任务提交：

```cpp
// CCClientProcess.cpp
void CCClientProcess::StartClientProcessWithSockfd(int sockfd, 
                                                    ThreadPool* recvPool, 
                                                    ThreadPool* sendPool)
{
    m_clientSockfd = sockfd;
    m_pRecvPool = recvPool;
    m_pSendPool = sendPool;
    
    // 启动接收任务循环
    StartRecvTaskLoop();
    
    // 启动发送任务循环
    StartSendTaskLoop();
}

void CCClientProcess::StartRecvTaskLoop()
{
    // 提交第一个接收任务
    m_pRecvPool->EnqueueTask([this]() {
        RunRecvProcessOnce();  // 执行一次接收逻辑
        
        // 如果还在运行，继续提交下一个任务
        if (m_bClientRunning) {
            StartRecvTaskLoop();
        }
    });
}

void CCClientProcess::RunRecvProcessOnce()
{
    // 原来的RunRecvProcess()逻辑，但去掉while循环
    // 每次只处理一次接收操作
    CC_MsgHeader msgHeader;
    memset((char*)&msgHeader, 0, sizeof(CC_MsgHeader));
    
    if (recvSocketData((char*)&msgHeader, sizeof(CC_MsgHeader))) {
        if (strcmp((char*)msgHeader.headerID, "CCTC") == 0 &&
            msgHeader.msgType == MSGHEADER_TYPE_KEEPALIVE) {
            // 收到心跳
        }
    }
    
    // 检查超时
    // ...
}
```

## 性能对比

| 指标 | 当前实现（每客户端2线程） | 线程池方案（16线程） |
|------|------------------------|-------------------|
| 100客户端线程数 | 200个线程 | 16个线程 |
| 线程创建开销 | 高（频繁创建/销毁） | 低（预先创建，复用） |
| 内存占用 | 高（每个线程栈空间） | 低（固定线程数） |
| 系统资源 | 可能耗尽 | 可控 |
| 任务调度 | 无调度（线程阻塞） | 有调度（任务队列） |

## 注意事项

1. **任务粒度**：接收/发送任务应该是短时间的，避免长时间占用线程
2. **任务频率**：需要控制任务提交频率，避免队列堆积
3. **线程数量**：根据CPU核心数和任务特性调整（建议：CPU核心数 * 2）
4. **异常处理**：任务执行异常不应影响线程池运行

## 总结

使用线程池可以：
- ✅ 控制并发数量，避免资源耗尽
- ✅ 复用线程，减少创建/销毁开销
- ✅ 更好的任务调度和负载均衡
- ✅ 更容易监控和管理

缺点是：
- ❌ 需要改造现有代码
- ❌ 任务需要封装成函数对象
- ❌ 需要处理任务队列堆积的情况


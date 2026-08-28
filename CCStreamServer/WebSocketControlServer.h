#ifndef WEBSOCKETCONTROLSERVER_H
#define WEBSOCKETCONTROLSERVER_H

#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>

// 使用libwebsockets库（需要安装libwebsockets-dev）
// 如果系统没有，可以使用其他WebSocket库如uWebSockets
extern "C" {
#include <libwebsockets.h>
}

// 前向声明
struct BeautyFilterParams;

/**
 * @brief WebSocket控制命令类型
 */
enum class ControlCommand {
    UPDATE_FILTER,      // 更新美颜滤镜参数
    START_STREAM,       // 开始推流
    STOP_STREAM,        // 停止推流
    ADD_RTMP_URL,       // 添加RTMP推流地址
    REMOVE_RTMP_URL,    // 移除RTMP推流地址
    GET_STATUS,         // 获取服务器状态
    UNKNOWN
};

/**
 * @brief 控制命令回调函数类型
 */
typedef std::function<void(const std::string& jsonData)> ControlCommandCallback;

/**
 * @brief WebSocket控制服务器
 * 用于接收Qt客户端发送的控制命令（美颜参数、推流控制等）
 * 使用libwebsockets实现
 */
class WebSocketControlServer
{
public:
    WebSocketControlServer();
    ~WebSocketControlServer();

    /**
     * @brief 启动WebSocket服务器
     * @param port 监听端口（默认8080）
     * @return 成功返回true
     */
    bool Start(int port = 8080);

    /**
     * @brief 停止WebSocket服务器
     */
    void Stop();

    /**
     * @brief 注册命令回调函数
     * @param command 命令类型
     * @param callback 回调函数
     */
    void RegisterCommandCallback(ControlCommand command, ControlCommandCallback callback);

    /**
     * @brief 发送消息到所有连接的客户端
     * @param message JSON格式消息
     */
    void BroadcastMessage(const std::string& message);

    /**
     * @brief 检查服务器是否运行
     */
    bool IsRunning() const { return m_bRunning; }

private:
    /**
     * @brief WebSocket协议回调（libwebsockets回调）
     */
    static int WebSocketCallback(struct lws *wsi, enum lws_callback_reasons reason,
                                 void *user, void *in, size_t len);

    /**
     * @brief 处理接收到的消息
     */
    void HandleMessage(const std::string& message, struct lws* wsi);

    /**
     * @brief 解析JSON命令
     */
    ControlCommand ParseCommand(const std::string& json);

    /**
     * @brief 服务器线程函数
     */
    void ServerThreadFunc();

private:
    std::atomic<bool> m_bRunning;           // 服务器运行状态
    int m_port;                             // 监听端口
    
    struct lws_context* m_context;           // libwebsockets上下文
    std::thread m_serverThread;             // 服务器线程
    
    std::map<ControlCommand, ControlCommandCallback> m_callbacks;  // 命令回调映射
    std::mutex m_callbacksMutex;            // 回调映射互斥锁
    
    std::vector<struct lws*> m_clients;      // 连接的客户端列表
    std::mutex m_clientsMutex;              // 客户端列表互斥锁
};

#endif // WEBSOCKETCONTROLSERVER_H


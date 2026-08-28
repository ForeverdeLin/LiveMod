/**
 * @file WebSocketControlServer.cpp
 * @brief WebSocket控制服务器实现
 * @details 使用libwebsockets库实现WebSocket服务器，接收客户端控制命令
 *          支持：美颜参数调整、RTMP推流控制、状态查询等
 *          知识点：libwebsockets API、WebSocket协议、JSON解析、多客户端管理
 */

#include "WebSocketControlServer.h"
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <algorithm>

// libwebsockets协议名称
static const char* PROTOCOL_NAME = "control-protocol";

/**
 * @brief 从JSON字符串解析控制命令
 * @details 通过字符串匹配方式解析JSON命令，查找action字段判断命令类型。
 *          支持update_filter、start_stream、stop_stream、add_rtmp_url、remove_rtmp_url、get_status等命令。
 *          注意：这是简化实现，实际项目应使用jsoncpp或nlohmann/json库
 * @param json JSON格式的命令字符串
 * @return ControlCommand枚举值
 */
static ControlCommand ParseCommandFromJSON(const std::string& json)
{
    // 简单的字符串匹配（实际应该使用JSON解析库）
    if (json.find("\"action\":\"update_filter\"") != std::string::npos ||
        json.find("'action':'update_filter'") != std::string::npos) {
        return ControlCommand::UPDATE_FILTER;
    } else if (json.find("\"action\":\"start_stream\"") != std::string::npos) {
        return ControlCommand::START_STREAM;
    } else if (json.find("\"action\":\"stop_stream\"") != std::string::npos) {
        return ControlCommand::STOP_STREAM;
    } else if (json.find("\"action\":\"add_rtmp_url\"") != std::string::npos) {
        return ControlCommand::ADD_RTMP_URL;
    } else if (json.find("\"action\":\"remove_rtmp_url\"") != std::string::npos) {
        return ControlCommand::REMOVE_RTMP_URL;
    } else if (json.find("\"action\":\"get_status\"") != std::string::npos) {
        return ControlCommand::GET_STATUS;
    }
    return ControlCommand::UNKNOWN;
}

/**
 * @brief WebSocket回调函数（libwebsockets）
 * @details libwebsockets的事件回调函数，根据reason类型处理不同事件。
 *          连接建立时将客户端加入列表；接收消息时复制数据并调用HandleMessage处理；
 *          连接关闭时从列表中移除客户端。使用互斥锁保护客户端列表的线程安全。
 * @param wsi WebSocket实例指针，标识客户端
 * @param reason 回调原因（事件类型）
 * @param user 用户数据指针
 * @param in 输入数据（消息内容）
 * @param len 数据长度
 * @return 0=成功，其他值=错误
 */
int WebSocketControlServer::WebSocketCallback(struct lws *wsi, enum lws_callback_reasons reason,
                                             void *user, void *in, size_t len)
{
    // 知识点：获取服务器实例
    // lws_context_user - 从WebSocket上下文获取用户数据（服务器实例指针）
    WebSocketControlServer* server = (WebSocketControlServer*)lws_context_user(lws_get_context(wsi));

    switch (reason) {
        // 知识点：连接建立事件
        // LWS_CALLBACK_ESTABLISHED - WebSocket握手完成，连接已建立
        case LWS_CALLBACK_ESTABLISHED:
            printf("WebSocket client connected\n");
            {
                // 知识点：线程安全的客户端列表管理
                // std::lock_guard - RAII锁，自动加锁和解锁
                std::lock_guard<std::mutex> lock(server->m_clientsMutex);
                server->m_clients.push_back(wsi);  // 将新客户端加入列表
            }
            break;

        // 知识点：接收消息事件
        // LWS_CALLBACK_RECEIVE - 接收到客户端发送的消息
        case LWS_CALLBACK_RECEIVE:
            {
                // 知识点：复制消息数据
                // in指向的数据可能在下一次回调中被覆盖，需要复制
                char* message = new char[len + 1];
                memcpy(message, in, len);
                message[len] = '\0';  // 添加字符串结束符
                
                printf("WebSocket received: %s\n", message);
                
                // 处理消息（解析命令并执行）
                server->HandleMessage(std::string(message), wsi);
                
                delete[] message;  // 释放临时缓冲区
            }
            break;

        // 知识点：连接关闭事件
        // LWS_CALLBACK_CLOSED - WebSocket连接已关闭
        case LWS_CALLBACK_CLOSED:
            printf("WebSocket client disconnected\n");
            {
                // 从客户端列表中移除
                std::lock_guard<std::mutex> lock(server->m_clientsMutex);
                server->m_clients.erase(
                    std::remove(server->m_clients.begin(), server->m_clients.end(), wsi),
                    server->m_clients.end());
            }
            break;

        default:
            break;
    }

    return 0;
}

// 知识点：WebSocket协议列表
// lws_protocols - libwebsockets协议结构数组
// 定义WebSocket协议的名称、回调函数、缓冲区大小等
static struct lws_protocols protocols[] = {
    {
        PROTOCOL_NAME,                              // 协议名称
        WebSocketControlServer::WebSocketCallback,  // 回调函数
        0,                                          // 每个连接的用户数据大小
        4096,                                       // 接收缓冲区大小（字节）
    },
    { NULL, NULL, 0, 0 } // 终止符（数组结束标记）
};

/**
 * @brief 构造函数
 * @details 初始化成员变量：运行状态为false，默认端口8080，上下文指针为空。
 */
WebSocketControlServer::WebSocketControlServer()
    : m_bRunning(false)
    , m_port(8080)
    , m_context(nullptr)
{
}

/**
 * @brief 析构函数
 * @details 自动调用Stop()停止服务器，确保资源正确释放。
 */
WebSocketControlServer::~WebSocketControlServer()
{
    Stop();
}

/**
 * @brief 启动WebSocket服务器
 * @details 检查运行状态，创建libwebsockets上下文并配置端口和协议，将服务器实例指针传递给上下文。
 *          创建成功后启动独立线程运行ServerThreadFunc处理WebSocket事件。
 * @param port 监听端口号
 * @return 成功返回true，失败返回false
 */
bool WebSocketControlServer::Start(int port)
{
    if (m_bRunning) {
        printf("WebSocket server already running\n");
        return false;
    }

    m_port = port;

    // 创建libwebsockets上下文
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.user = this; // 传递服务器实例指针

    m_context = lws_create_context(&info);
    if (!m_context) {
        printf("Failed to create libwebsockets context\n");
        return false;
    }

    m_bRunning = true;

    // 启动服务器线程
    m_serverThread = std::thread(&WebSocketControlServer::ServerThreadFunc, this);

    printf("WebSocket control server started on port %d\n", port);

    return true;
}

/**
 * @brief 停止WebSocket服务器
 * @details 设置运行标志为false，等待服务器线程结束，然后销毁libwebsockets上下文释放资源。
 */
void WebSocketControlServer::Stop()
{
    if (!m_bRunning) {
        return;
    }

    m_bRunning = false;

    // 等待线程结束
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    // 销毁上下文
    if (m_context) {
        lws_context_destroy(m_context);
        m_context = nullptr;
    }

    printf("WebSocket control server stopped\n");
}

/**
 * @brief 服务器线程函数
 * @details 在独立线程中循环调用lws_service处理WebSocket事件，每次调用50ms超时。
 *          当m_bRunning为false时退出循环，线程结束。
 */
void WebSocketControlServer::ServerThreadFunc()
{
    while (m_bRunning) {
        // 处理WebSocket事件
        lws_service(m_context, 50); // 50ms超时
    }
}

/**
 * @brief 注册命令回调函数
 * @details 使用互斥锁保护回调函数映射表，将命令类型与对应的回调函数关联存储。
 * @param command 控制命令类型
 * @param callback 回调函数
 */                   //传入字符串和一个函数
void WebSocketControlServer::RegisterCommandCallback(ControlCommand command, ControlCommandCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_callbacks[command] = callback;
}

/**
 * @brief 处理接收到的消息
 * @details 解析JSON消息获取命令类型，在回调映射表中查找对应的回调函数并执行。
 *          如果未找到注册的回调函数则打印警告信息。使用互斥锁保护回调映射表的访问。
 * @param message 接收到的JSON消息字符串
 * @param wsi WebSocket实例指针（当前未使用）
 */
void WebSocketControlServer::HandleMessage(const std::string& message, struct lws* wsi)
{
    // 解析命令
    ControlCommand cmd = ParseCommandFromJSON(message);

    // 查找回调函数
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    auto it = m_callbacks.find(cmd);
    if (it != m_callbacks.end()) {
        // 调用回调函数
        it->second(message);
    } else {
        printf("No callback registered for command: %d\n", (int)cmd);
    }
}

/**
 * @brief 解析命令（公共接口）
 * @details 封装静态函数ParseCommandFromJSON，提供公共接口供外部调用。
 * @param json JSON格式的命令字符串
 * @return ControlCommand枚举值
 */
ControlCommand WebSocketControlServer::ParseCommand(const std::string& json)
{
    return ParseCommandFromJSON(json);
}

/**
 * @brief 广播消息给所有连接的客户端
 * @details 遍历所有客户端连接，为每个连接分配缓冲区（包含LWS_PRE前缀空间），
 *          将消息数据复制到缓冲区后调用lws_write发送文本消息，最后释放缓冲区。
 *          使用互斥锁保护客户端列表的访问。
 * @param message 要广播的消息字符串
 */
void WebSocketControlServer::BroadcastMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);

    for (auto* wsi : m_clients) {
        unsigned char* buf = new unsigned char[LWS_PRE + message.size()];
        memcpy(buf + LWS_PRE, message.c_str(), message.size());

        lws_write(wsi, buf + LWS_PRE, message.size(), LWS_WRITE_TEXT);
        delete[] buf;
    }
}


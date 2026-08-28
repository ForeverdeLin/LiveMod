#ifndef WEBSOCKETCONTROLCLIENT_H
#define WEBSOCKETCONTROLCLIENT_H

#include <QObject>
#include <QWebSocket>
#include <QUrl>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>

/**
 * @brief WebSocket控制客户端
 * 用于向服务器发送控制命令（美颜参数、推流控制等）
 */
class WebSocketControlClient : public QObject
{
    /*继承 QObject 后可使用 Qt 的信号槽机制           ！！！！！！！！
      支持 Qt 的对象树和内存管理;
      可使用 Q_OBJECT 宏启用元对象系统（包括信号槽机制connect，反射*/
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针（默认 nullptr）
     * 显式构造函数explicit禁止隐式类型转换
     * 父对象指针，默认为空
     * 用于 Qt 对象树管理
     * 父对象销毁时，子对象会自动销毁
     * nullptr 表示无父对象
     */
    explicit WebSocketControlClient(QObject *parent = nullptr);
    ~WebSocketControlClient();



    /**
     * @brief 连接到服务器
     * @param url WebSocket服务器地址（如 ws://192.168.1.100:8080）
     */
    void connectToServer(const QString& url);

    /**
     * @brief 断开连接
     */
    void disconnectFromServer();

    /**
     * @brief 更新美颜滤镜参数
     * @param smoothLevel 磨皮强度 (0.0-1.0)
     * @param brightLevel 亮度 (-1.0-1.0)
     * @param contrastLevel 对比度 (-1.0-1.0)
     * @param saturationLevel 饱和度 (-1.0-1.0)
     */
    void updateFilterParams(float smoothLevel, float brightLevel, 
                           float contrastLevel, float saturationLevel);

    /**
     * @brief 开始推流
     */
    void startStreaming();

    /**
     * @brief 停止推流
     */
    void stopStreaming();

    /**
     * @brief 添加RTMP推流地址
     * @param rtmpUrl RTMP地址（如 rtmp://live.bilibili.com/...）
     */
    void addRTMPUrl(const QString& rtmpUrl);

    /**
     * @brief 移除RTMP推流地址
     * @param rtmpUrl RTMP地址
     */
    void removeRTMPUrl(const QString& rtmpUrl);

    /**
     * @brief 获取服务器状态
     */
    void getStatus();

    /**
     * @brief 检查是否已连接
     */
    bool isConnected() const;

signals:
    /**
     * @brief 连接状态改变信号
     */
    void connected();
    void disconnected();
    
    /**
     * @brief 收到服务器消息信号
     */
    void messageReceived(const QString& message);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& message);
    void onError(QAbstractSocket::SocketError error);

private:
    /**
     * @brief 发送JSON命令
     */
    void sendCommand(const QJsonObject& command);

private:
    QWebSocket* m_webSocket;
    bool m_connected;
};

#endif // WEBSOCKETCONTROLCLIENT_H


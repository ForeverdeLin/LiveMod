#include "WebSocketControlClient.h"
#include <QDebug>

WebSocketControlClient::WebSocketControlClient(QObject *parent)
    : QObject(parent)
    , m_webSocket(nullptr)
    , m_connected(false)
{
    m_webSocket = new QWebSocket("", QWebSocketProtocol::VersionLatest, this);

    //作用：将 Qt 库的 QWebSocket 信号，信号是他自己定义的
    //连接到 WebSocketControlClient 的槽函数
    // 连接信号，断开信号，客户端收到消息信号，错误信号，，，，，这里是绑定
    //信号发送者，QT发出信号，接受者，槽函数
    connect(m_webSocket, &QWebSocket::connected, this, &WebSocketControlClient::onConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &WebSocketControlClient::onDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, 
            this, &WebSocketControlClient::onTextMessageReceived);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &WebSocketControlClient::onError);
}

WebSocketControlClient::~WebSocketControlClient()
{
    disconnectFromServer();
}




void WebSocketControlClient::connectToServer(const QString& url)//传入m_webSocketUrl，服务器地址
{
    if (m_connected) {
        qDebug() << "Already connected to server";
        return;
    }

    qDebug() << "Connecting to WebSocket server:" << url;
    QUrl wsUrl(url);//将字符串URL转换为QUrl对象
    //类型要求：QWebSocket::open() 需要 QUrl 类型，不能直接使用字符串
    //URL 解析：QUrl 可解析协议、主机、端口、路径等
    m_webSocket->open(wsUrl);//QT库函数，不是自己定义的
}

void WebSocketControlClient::disconnectFromServer()
{
    if (m_webSocket && m_webSocket->state() == QAbstractSocket::ConnectedState) {
        m_webSocket->close();
    }
}









void WebSocketControlClient::updateFilterParams(float smoothLevel, float brightLevel,
                                               float contrastLevel, float saturationLevel)
{
    QJsonObject command;
    command["action"] = "update_filter";
    command["params"] = QJsonObject({
        {"smooth", smoothLevel},
        {"bright", brightLevel},
        {"contrast", contrastLevel},
        {"saturation", saturationLevel}
    });

    sendCommand(command);
}

void WebSocketControlClient::startStreaming()
{
    QJsonObject command;
    command["action"] = "start_stream";
    sendCommand(command);
}

void WebSocketControlClient::stopStreaming()
{
    QJsonObject command;
    command["action"] = "stop_stream";
    sendCommand(command);
}

void WebSocketControlClient::addRTMPUrl(const QString& rtmpUrl)
{
    QJsonObject command;
    command["action"] = "add_rtmp_url";
    command["url"] = rtmpUrl;
    sendCommand(command);
}

void WebSocketControlClient::removeRTMPUrl(const QString& rtmpUrl)
{
    QJsonObject command;
    command["action"] = "remove_rtmp_url";
    command["url"] = rtmpUrl;
    sendCommand(command);
}

void WebSocketControlClient::getStatus()
{
    QJsonObject command;
    command["action"] = "get_status";
    sendCommand(command);
}

bool WebSocketControlClient::isConnected() const
{
    return m_connected;
}

void WebSocketControlClient::sendCommand(const QJsonObject& command)
{
    if (!m_connected) {
        qDebug() << "Not connected to server, cannot send command";
        return;
    }

    QJsonDocument doc(command);
    QString jsonString = doc.toJson(QJsonDocument::Compact);//转换为紧凑格式字符串
    
    qDebug() << "Sending command:" << jsonString;
    m_webSocket->sendTextMessage(jsonString);//// 在头文件中定义
    //QWebSocket* m_webSocket;  // QWebSocket 是 Qt 库的类
}












void WebSocketControlClient::onConnected()
{
    m_connected = true;
    qDebug() << "WebSocket connected to server";
    emit connected();//，自定义的信号，告诉编译器，websocket在发出一个信号，
    //会触发mainwindow里与connect绑定的信号函数（
}

void WebSocketControlClient::onDisconnected()
{
    m_connected = false;
    qDebug() << "WebSocket disconnected from server";
    emit disconnected();
}

void WebSocketControlClient::onTextMessageReceived(const QString& message)
{
    qDebug() << "Received message from server:" << message;
    emit messageReceived(message);//信号（向上层通知）
}

void WebSocketControlClient::onError(QAbstractSocket::SocketError error)
{
    qDebug() << "WebSocket error:" << error;
    m_connected = false;
}


/**
 * @file mainwindow.cpp
 * @brief Qt主窗口实现
 * @details 实现智能直播推流控制台的GUI界面
 *          功能：视频显示、美颜参数控制、RTMP推流控制、设备选择等
 *          知识点：Qt GUI编程、OpenGL视频渲染、WebSocket客户端、信号槽机制
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QAudioDevice>
#include <QStringList>

/**
 * @brief 构造函数
 * @details 初始化主窗口，创建所有UI组件和功能模块
 * @param parent 父窗口指针
 * @note 实现：设置UI界面，创建OpenGL视频显示组件，初始化UI控件和信号槽连接，
 *       枚举系统设备列表，建立WebSocket控制连接，配置音频输出，创建TCP视频客户端
 *       并设置视频/音频数据回调函数。
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)                    // UI对象（Qt Designer生成的界面类实例）
    , m_pVideoClient(nullptr)                   // TCP视频客户端指针（用于接收视频流数据，初始化为空）
    , m_pOpenGLWidget(nullptr)                  // OpenGL视频显示组件指针（用于渲染YUV视频帧，初始化为空）
    , m_pWebSocketClient(nullptr)               // WebSocket客户端指针（用于与服务器通信发送控制命令，初始化为空）
    , m_webSocketUrl("ws://192.168.236.101:8080") // WebSocket服务器连接地址（默认指向本地测试服务器）
    
    , m_smoothSlider(nullptr)                   // 美颜平滑度调节滑块指针（初始化为空，后续在initUI中绑定）
    , m_brightSlider(nullptr)                   // 亮度调节滑块指针（初始化为空，后续在initUI中绑定）
    , m_contrastSlider(nullptr)                 // 对比度调节滑块指针（初始化为空，后续在initUI中绑定）
    , m_saturationSlider(nullptr)               // 饱和度调节滑块指针（初始化为空，后续在initUI中绑定）
    , m_startStreamBtn(nullptr)                 // 开始推流按钮指针（初始化为空，后续在initUI中绑定）
    , m_stopStreamBtn(nullptr)                  // 停止推流按钮指针（初始化为空，后续在initUI中绑定）
    , m_rtmpUrlEdit(nullptr)                    // RTMP地址输入框指针（初始化为空，后续在initUI中绑定）
    , m_rtmpUrlList(nullptr)                    // RTMP地址列表控件指针（初始化为空，后续在initUI中绑定）
    , m_addRtmpUrlBtn(nullptr)                  // 添加RTMP地址按钮指针（初始化为空，后续在initUI中绑定）
    , m_removeRtmpUrlBtn(nullptr)               // 移除RTMP地址按钮指针（初始化为空，后续在initUI中绑定）
    , m_cameraCombo(nullptr)                    // 摄像头选择下拉框指针（初始化为空，后续在initUI中绑定）
    , m_microphoneCombo(nullptr)                // 麦克风选择下拉框指针（初始化为空，后续在initUI中绑定）
    , m_statusLabel(nullptr)                    // 状态显示标签指针（初始化为空，后续在initUI中绑定）
    , m_smoothLevel(0.5f)                       // 平滑度初始值（范围0.0-1.0，默认0.5）
    , m_brightLevel(0.1f)                       // 亮度初始值（范围0.0-1.0，默认0.1）
    , m_contrastLevel(0.1f)                     // 对比度初始值（范围0.0-1.0，默认0.1）
    , m_saturationLevel(0.0f)                   // 饱和度初始值（范围0.0-1.0，默认0.0）
    , m_bStreaming(false)                       // 推流状态标志（默认未推流）
    , m_pAudioOutput(nullptr)                   // 音频输出对象指针（用于播放PCM音频，初始化为空）
    , m_pAudioIODevice(nullptr)                 // 音频I/O设备指针（音频输出的写入接口，初始化为空）
{
    // 知识点：设置UI（Qt Designer生成的UI文件）
    ui->setupUi(this);//加载UI文件
    setWindowTitle("智能直播推流控制台");

    // 知识点：创建OpenGL视频显示组件
    // 必须在initUI之前创建，因为initUI中会使用它,  initUI只是添加了组件，具体布局得到show
    m_pOpenGLWidget = new CCOpenGLWidget(this);
    
    // 初始化UI组件（按钮、滑块、列表等）
    initUI();
    
    // 初始化设备列表（摄像头、麦克风）
    initDevices();
    
    // 初始化WebSocket连接（用于发送控制命令到服务器）
    initWebSocket();
    
    // 初始化音频输出（用于播放接收到的音频流）
    initAudioOutput();
    
    // 知识点：连接视频服务器（TCP模式）
    // 从流媒体服务器接收H.264/AAC数据流
    CC_NetConnectInfo networkInfo;
    memset(&networkInfo, 0, sizeof(CC_NetConnectInfo));
    char* serverIP = (char*)"192.168.236.101";
    strncpy(networkInfo.server_ip, serverIP, sizeof(networkInfo.server_ip));
    networkInfo.port = 30000;

    // 知识点：创建TCP视频客户端
    m_pVideoClient = new CCVideoClient();
    m_pVideoClient->StartSocketConnection(&networkInfo);  // 启动TCP连接
    
    // 知识点：设置回调函数
    // 当接收到视频/音频数据时，调用这些静态成员函数更新GUI
    m_pVideoClient->SetupUpdateGUICallback(MainWindow::updateVideoData, (unsigned long)this);
    m_pVideoClient->SetupUpdateAudioCallback(MainWindow::updateAudioData, (unsigned long)this);
}



/**
 * @brief 析构函数
 * @details 释放所有资源
 * @note 实现：依次停止并删除TCP视频客户端、OpenGL组件、WebSocket客户端、音频输出设备，最后删除UI对象。
 */
MainWindow::~MainWindow()
{
    if(m_pVideoClient != nullptr)
    {
        m_pVideoClient->StopSocketClient();
        delete m_pVideoClient;
        m_pVideoClient = nullptr;
    }

    if(m_pOpenGLWidget != nullptr)
    {
        delete m_pOpenGLWidget;
        m_pOpenGLWidget = nullptr;
    }
    
    if(m_pWebSocketClient != nullptr)
    {
        m_pWebSocketClient->disconnectFromServer();
        delete m_pWebSocketClient;
        m_pWebSocketClient = nullptr;
    }
    
    // 清理音频输出
    if(m_pAudioIODevice != nullptr)
    {
        m_pAudioIODevice->close();
        delete m_pAudioIODevice;
        m_pAudioIODevice = nullptr;
    }
    if(m_pAudioOutput != nullptr)
    {
        m_pAudioOutput->stop();
        delete m_pAudioOutput;
        m_pAudioOutput = nullptr;
    }

    delete ui;
}


//初始化UI里绑定图形与信号槽函数



/**
 * @brief 初始化UI组件
 * @details 从UI文件获取控件指针，设置布局，连接信号槽
 * @note 实现：从ui对象获取所有 控件指针 并保存到成员变量，将OpenGL组件 添加 到视频显示区域的布局中，
 *       连接所有滑块、按钮、下拉框的信号到对应的槽函数。
 */
void MainWindow::initUI()
{
    // 从UI文件加载控件（UI文件已经通过ui->setupUi(this)加载）  图形化配置界面，和mainwindow.ui文件对应
    // 获取UI文件中的控件指针
    m_smoothSlider = ui->smoothSlider;
    m_brightSlider = ui->brightSlider;
    m_contrastSlider = ui->contrastSlider;
    m_saturationSlider = ui->saturationSlider;
    
    m_startStreamBtn = ui->startStreamBtn;
    m_stopStreamBtn = ui->stopStreamBtn;
    m_rtmpUrlEdit = ui->rtmpUrlEdit;
    m_rtmpUrlList = ui->rtmpUrlList;
    m_addRtmpUrlBtn = ui->addRtmpUrlBtn;
    m_removeRtmpUrlBtn = ui->removeRtmpUrlBtn;
    
    m_cameraCombo = ui->cameraCombo;
    m_microphoneCombo = ui->microphoneCombo;
    
    m_statusLabel = ui->statusLabel;
    
    // 将OpenGL组件添加到视频显示区域
    QWidget* videoWidget = ui->videoWidget;
    //layout() 返回的是基类指针 QLayout*，需要转换为具体类型 QVBoxLayout* 才能调用 addWidget
    //QLayout：布局的 “通用模板”，只定义规则，不干活； 抽象基类
    //• QVBoxLayout：垂直布局的 “具体工人”，按垂直规则排列组件，能直接实例化和调用。具体子类
    QVBoxLayout* videoLayout = qobject_cast<QVBoxLayout*>(videoWidget->layout());
    if (videoLayout) {
        videoLayout->addWidget(m_pOpenGLWidget);
    }
    
    // 组件连接信号槽
    //. 第一个参数：信号发送者；第二个参数：信号
    //第三个参数：槽函数接收者（this就是mainwindow）第四个参数：槽函数
    connect(m_smoothSlider, &QSlider::valueChanged, this, &MainWindow::onSmoothSliderChanged);
    connect(m_brightSlider, &QSlider::valueChanged, this, &MainWindow::onBrightSliderChanged);
    connect(m_contrastSlider, &QSlider::valueChanged, this, &MainWindow::onContrastSliderChanged);
    connect(m_saturationSlider, &QSlider::valueChanged, this, &MainWindow::onSaturationSliderChanged);
    
    connect(m_startStreamBtn, &QPushButton::clicked, this, &MainWindow::onStartStreamClicked);
    connect(m_stopStreamBtn, &QPushButton::clicked, this, &MainWindow::onStopStreamClicked);
    
    connect(m_addRtmpUrlBtn, &QPushButton::clicked, this, &MainWindow::onAddRTMPUrlClicked);
    connect(m_removeRtmpUrlBtn, &QPushButton::clicked, this, &MainWindow::onRemoveRTMPUrlClicked);
    //带int参数（选中项的索引）currentIndexChanged
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onCameraChanged);
    connect(m_microphoneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onMicrophoneChanged);
}






/**
 * @brief 初始化设备列表
 * @details 枚举系统可用的摄像头和麦克风设备
 * @note 实现：使用QMediaDevices获取所有视频输入和音频输入设备，清空下拉框后逐个添加设备描述到下拉框。
 */
void MainWindow::initDevices()
{
    // 获取摄像头列表
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    m_cameraCombo->clear();//清空下拉框中的所有选项
    for (const QCameraDevice& camera : cameras) {//const&只读，避免拷贝
        m_cameraCombo->addItem(camera.description());
        //获取摄像头设备的描述名称（字符串;添加到下拉框
    }
    
    // 获取麦克风列表
    const QList<QAudioDevice> microphones = QMediaDevices::audioInputs();
    m_microphoneCombo->clear();
    for (const QAudioDevice& mic : microphones) {
        m_microphoneCombo->addItem(mic.description());
    }
}








/**
 * @brief 初始化WebSocket客户端
 * @details 创建WebSocket客户端实例，配置与服务器通信的信号槽连接（连接状态变化、消息接收），
 *          并启动与指定WebSocket服务器的连接，用于实现控制命令（如美颜参数、推流控制）的双向传输。
 * @note 实现：创建WebSocketControlClient实例，连接connected/disconnected/messageReceived信号到对应槽函数，
 *       然后调用connectToServer连接到指定URL。
 */
void MainWindow::initWebSocket()
{   //这里+this,mainwindow析构了他自动清理
    m_pWebSocketClient = new WebSocketControlClient(this);
    
    //作用：将 WebSocketControlClient 的自定义信号连接到 MainWindow 的槽函数
    //信号在websocketcpp里定义了    ，接收到第二个参数的信号，触发第四个参数的函数更新UI


    connect(m_pWebSocketClient, &WebSocketControlClient::connected,//接收emit connected()
            this, &MainWindow::onWebSocketConnected);
    connect(m_pWebSocketClient, &WebSocketControlClient::disconnected,
            this, &MainWindow::onWebSocketDisconnected);
    connect(m_pWebSocketClient, &WebSocketControlClient::messageReceived,
            this, &MainWindow::onWebSocketMessageReceived);//在下面
    
    // 连接到服务器
    m_pWebSocketClient->connectToServer(m_webSocketUrl);//上面初始化定义了
}






/**
 * @brief 初始化音频输出
 * @details 配置音频格式并创建音频输出设备
 * @note 实现：设置音频格式为44.1kHz采样率、16位深度、双声道，创建QAudioOutput对象并启动，
 *       获取音频I/O设备指针用于后续写入PCM数据。
 */
void MainWindow::initAudioOutput()
{
    // 设置音频格式：44.1kHz，16位，立体声
    m_audioFormat.setSampleRate(44100);
    m_audioFormat.setChannelCount(2);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    
    // 创建音频输出
    m_pAudioOutput = new QAudioOutput(m_audioFormat, this);
    m_pAudioIODevice = m_pAudioOutput->start();
    
    if(m_pAudioIODevice == nullptr)
    {
        qDebug() << "Failed to start audio output";
    }
    else
    {
        qDebug() << "Audio output initialized: 44100Hz, 16bit, 2 channels";
    }
}









/**
 * @brief 更新美颜参数到服务器，由槽函数触发
 * @details 将当前美颜参数发送到服务器
 * @note 实现：检查WebSocket连接状态，如果已连接则调用updateFilterParams方法将平滑度、亮度、对比度、饱和度参数发送到服务器。
 */
void MainWindow::updateFilterParamsToServer()
{
    if (m_pWebSocketClient && m_pWebSocketClient->isConnected()) {
        m_pWebSocketClient->updateFilterParams(
            m_smoothLevel, m_brightLevel, m_contrastLevel, m_saturationLevel);
    }
}

/**
 * @brief 平滑度滑块值改变槽函数
 * @details 响应平滑度滑块值变化
 * @note 实现：将滑块整数值（0-100）转换为浮点数（0.0-1.0）保存到成员变量，然后调用updateFilterParamsToServer发送到服务器。
 */
void MainWindow::onSmoothSliderChanged(int value)
{
    m_smoothLevel = value / 100.0f;
    updateFilterParamsToServer();
}

/**
 * @brief 亮度滑块值改变槽函数
 * @details 响应亮度滑块值变化
 * @note 实现：将滑块整数值（0-100）转换为浮点数（0.0-1.0）保存到成员变量，然后调用updateFilterParamsToServer发送到服务器。
 */
void MainWindow::onBrightSliderChanged(int value)
{
    m_brightLevel = value / 100.0f;
    updateFilterParamsToServer();
}

/**
 * @brief 对比度滑块值改变槽函数
 * @details 响应对比度滑块值变化
 * @note 实现：将滑块整数值（0-100）转换为浮点数（0.0-1.0）保存到成员变量，然后调用updateFilterParamsToServer发送到服务器。
 */
void MainWindow::onContrastSliderChanged(int value)
{
    m_contrastLevel = value / 100.0f;
    updateFilterParamsToServer();
}

/**
 * @brief 饱和度滑块值改变槽函数
 * @details 响应饱和度滑块值变化
 * @note 实现：将滑块整数值（0-100）转换为浮点数（0.0-1.0）保存到成员变量，然后调用updateFilterParamsToServer发送到服务器。
 */
void MainWindow::onSaturationSliderChanged(int value)
{
    m_saturationLevel = value / 100.0f;
    updateFilterParamsToServer();
}





/**
 * @brief 开始推流按钮点击槽函数
 * @details 响应开始推流按钮点击事件
 * @note 实现：检查WebSocket连接状态，如果已连接则发送startStreaming命令到服务器，更新推流状态标志，
 *       禁用开始按钮、启用停止按钮，更新状态标签显示"推流中..."。
 */
void MainWindow::onStartStreamClicked()
{
    if (m_pWebSocketClient && m_pWebSocketClient->isConnected()) {
        m_pWebSocketClient->startStreaming();
        m_bStreaming = true;
        m_startStreamBtn->setEnabled(false);
        //禁用“开始推流”按钮
        //推流开始后，防止重复点击
        m_stopStreamBtn->setEnabled(true);
        m_statusLabel->setText("状态: 推流中...");
    }
}

/**
 * @brief 停止推流按钮点击槽函数
 * @details 响应停止推流按钮点击事件
 * @note 实现：检查WebSocket连接状态，如果已连接则发送stopStreaming命令到服务器，更新推流状态标志，
 *       启用开始按钮、禁用停止按钮，更新状态标签显示"推流已停止"。
 */
void MainWindow::onStopStreamClicked()
{
    if (m_pWebSocketClient && m_pWebSocketClient->isConnected()) {
        m_pWebSocketClient->stopStreaming();
        m_bStreaming = false;
        m_startStreamBtn->setEnabled(true);
        m_stopStreamBtn->setEnabled(false);
        m_statusLabel->setText("状态: 推流已停止");
    }
}

/**
 * @brief 添加RTMP地址按钮点击槽函数
 * @details 响应添加RTMP地址按钮点击事件
 * @note 实现：从输入框获取URL文本并去除首尾空格，如果不为空则添加到列表控件，如果WebSocket已连接则
 * 发送addRTMPUrl命令到服务器，最后清空输入框。
 */
void MainWindow::onAddRTMPUrlClicked()
{
    QString url = m_rtmpUrlEdit->text().trimmed();
    if (!url.isEmpty()) {
        m_rtmpUrlList->addItem(url);
        if (m_pWebSocketClient && m_pWebSocketClient->isConnected()) {
            m_pWebSocketClient->addRTMPUrl(url);
        }
        m_rtmpUrlEdit->clear();
    }
}

/**
 * @brief 移除RTMP地址按钮点击槽函数
 * @details 响应移除RTMP地址按钮点击事件
 * @note 实现：获取列表控件当前选中行的索引，如果有效则获取该行的URL文本，
 * 从列表中移除该项，如果WebSocket已连接则发送removeRTMPUrl命令到服务器。
 */
void MainWindow::onRemoveRTMPUrlClicked()
{
    int row = m_rtmpUrlList->currentRow();
    if (row >= 0) {
        QString url = m_rtmpUrlList->item(row)->text();
        m_rtmpUrlList->takeItem(row);
        if (m_pWebSocketClient && m_pWebSocketClient->isConnected()) {
            m_pWebSocketClient->removeRTMPUrl(url);
        }
    }
}

/**
 * @brief WebSocket连接成功槽函数
 * @details 响应WebSocket连接成功信号
 * @note 实现：更新状态标签显示"WebSocket已连接"，并输出调试信息到控制台。
 */
void MainWindow::onWebSocketConnected()
{
    m_statusLabel->setText("状态: WebSocket已连接");
    qDebug() << "WebSocket connected";
}

/**
 * @brief WebSocket断开连接槽函数
 * @details 响应WebSocket断开连接信号
 * @note 实现：更新状态标签显示"WebSocket已断开"，并输出调试信息到控制台。
 */
void MainWindow::onWebSocketDisconnected()
{
    m_statusLabel->setText("状态: WebSocket已断开");
    qDebug() << "WebSocket disconnected";
}

/**
 * @brief WebSocket消息接收槽函数
 * @details 响应WebSocket接收到服务器消息信号
 * @note 实现：将接收到的消息输出到调试控制台，预留处理服务器返回状态信息的接口。
 */
void MainWindow::onWebSocketMessageReceived(const QString& message)
{
    qDebug() << "Received message:" << message;
    // 可以在这里处理服务器返回的状态信息
}




/**
 * @brief 更新视频数据静态回调函数
 * @details TCP视频客户端接收到视频数据时调用的回调函数
 * @note 实现：从userData参数获取MainWindow实例指针，如果有效则调用实例的updateYUVFrame方法更新视频帧。
 */
void MainWindow::updateVideoData(YUVData_Frame* yuvFrame, unsigned long userData)
{
    MainWindow* window = (MainWindow*)userData;
    if(window != nullptr){
        window->updateYUVFrame(yuvFrame);
    }
}

/**
 * @brief 更新YUV视频帧
 * @details 将接收到的YUV视频帧传递给OpenGL组件渲染
 * @note 实现：检查OpenGL组件指针是否有效，如果有效则调用RendVideo方法将YUV帧数据传递给OpenGL组件进行渲染显示。
 */
void MainWindow::updateYUVFrame(YUVData_Frame* yuvFrame)
{
    if (m_pOpenGLWidget) {
        m_pOpenGLWidget->RendVideo(yuvFrame);
    }
}

/**
 * @brief 更新音频数据静态回调函数
 * @details TCP视频客户端接收到音频数据时调用的回调函数
 * @note 实现：从userData参数获取MainWindow实例指针，如果有效则调用实例的playPCMData方法播放音频数据。
 */
void MainWindow::updateAudioData(uint8_t* pcmData, int pcmSize, unsigned long userData)
{
    MainWindow* window = (MainWindow*)userData;
    if(window != nullptr){
        window->playPCMData(pcmData, pcmSize);
    }
}

/**
 * @brief 播放PCM音频数据
 * @details 将接收到的PCM音频数据写入音频输出设备
 * @note 实现：检查音频I/O设备指针、PCM数据指针和大小是否有效，如果有效则将PCM数据写入音频输出设备，
 *       如果写入的字节数不完整则输出警告信息。
 */
void MainWindow::playPCMData(uint8_t* pcmData, int pcmSize)
{
    if(m_pAudioIODevice != nullptr && pcmData != nullptr && pcmSize > 0)
    {
        // 写入PCM数据到音频输出设备
        qint64 written = m_pAudioIODevice->write((const char*)pcmData, pcmSize);
        if(written != pcmSize)
        {
            qDebug() << "Audio write incomplete:" << written << "of" << pcmSize;
        }
    }
}

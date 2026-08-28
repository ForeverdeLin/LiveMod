#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QIODevice>
#include "CCVideoClient.h"
#include "CCOpenGLWidget.h"
#include "WebSocketControlClient.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/**
 * @brief 智能直播控制台主窗口
 * 提供视频预览、美颜滤镜控制、推流管理等功能
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    /**
     * @brief 美颜滤镜参数改变槽函数
     */
    void onSmoothSliderChanged(int value);
    void onBrightSliderChanged(int value);
    void onContrastSliderChanged(int value);
    void onSaturationSliderChanged(int value);
    
    /**
     * @brief 推流控制槽函数
     */
    void onStartStreamClicked();
    void onStopStreamClicked();
    void onAddRTMPUrlClicked();
    void onRemoveRTMPUrlClicked();
    
    /**
     * @brief WebSocket连接槽函数
     */
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onWebSocketMessageReceived(const QString& message);
    
    /**
     * @brief 设备选择槽函数
     */
    void onCameraChanged(int index);
    void onMicrophoneChanged(int index);

private:
    /**
     * @brief 初始化UI控件
     */
    void initUI();
    
    /**
     * @brief 初始化设备列表
     */
    void initDevices();
    
    /**
     * @brief 初始化WebSocket连接
     */
    void initWebSocket();
    
    /**
     * @brief 更新美颜参数到服务器
     */
    void updateFilterParamsToServer();
    
    /**
     * @brief 视频数据更新回调（静态函数）
     */
    static void updateVideoData(YUVData_Frame* yuvFrame, unsigned long userData);
    
    /**
     * @brief 更新YUV帧到OpenGL渲染
     */
    void updateYUVFrame(YUVData_Frame* yuvFrame);
    
    /**
     * @brief 音频数据更新回调（静态函数）
     */
    static void updateAudioData(uint8_t* pcmData, int pcmSize, unsigned long userData);
    
    /**
     * @brief 播放PCM音频数据
     */
    void playPCMData(uint8_t* pcmData, int pcmSize);

private:
    Ui::MainWindow *ui;
    
    // 视频客户端
    CCVideoClient* m_pVideoClient;
    CCOpenGLWidget* m_pOpenGLWidget;
    
    // WebSocket控制客户端
    WebSocketControlClient* m_pWebSocketClient;
    QString m_webSocketUrl;  // WebSocket服务器地址
    
    // 美颜滤镜控件
    QSlider* m_smoothSlider;      // 磨皮滑块
    QSlider* m_brightSlider;       // 亮度滑块
    QSlider* m_contrastSlider;     // 对比度滑块
    QSlider* m_saturationSlider;   // 饱和度滑块
    
    // 推流控制控件
    QPushButton* m_startStreamBtn;
    QPushButton* m_stopStreamBtn;
    QLineEdit* m_rtmpUrlEdit;
    QListWidget* m_rtmpUrlList;
    QPushButton* m_addRtmpUrlBtn;
    QPushButton* m_removeRtmpUrlBtn;
    
    // 设备选择控件
    QComboBox* m_cameraCombo;
    QComboBox* m_microphoneCombo;
    
    // 状态标签
    QLabel* m_statusLabel;
    
    // 当前美颜参数
    float m_smoothLevel;
    float m_brightLevel;
    float m_contrastLevel;
    float m_saturationLevel;
    
    // 推流状态
    bool m_bStreaming;
    
    // 音频播放
    QAudioOutput* m_pAudioOutput;
    QIODevice* m_pAudioIODevice;
    QAudioFormat m_audioFormat;
};
#endif // MAINWINDOW_H

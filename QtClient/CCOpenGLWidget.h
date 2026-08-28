#ifndef CCOPENGLWIDGET_H
#define CCOPENGLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>

#include "CCYUVDataDefine.h"

#define ATTRIB_VERTEX 3
#define ATTRIB_TEXTURE 4

// OpenGL shader属性位置枚举，用于标识顶点坐标和纹理坐标属性
enum {
    ATTRIBUTE_VERTEX,    // 顶点坐标属性位置
    ATTRIBUTE_TEXCOORD   // 纹理坐标属性位置
};

// 自定义OpenGL窗口部件，继承QOpenGLWidget和QOpenGLFunctions，用于渲染YUV420P格式视频数据
class CCOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{//QOpenGLWidget提供一个可嵌入 Qt 界面的OpenGL 渲染区域；
//你的场景：作为自定义 OpenGL 窗口的载体，负责把 OpenGL 渲染的画面显示在 Qt 界面上。
//QOpenGLFunctions，提供统一的 API 调用；
//用于在 CCOpenGLWidget 中调用 OpenGL 函数，
//实现 YUV420P 视频帧的纹理上传、着色器渲染、画面绘制等操作。


public:
    // 构造函数
    CCOpenGLWidget(QWidget *parent = 0);
    // 析构函数
    ~CCOpenGLWidget();

    // 渲染YUV视频帧数据
    void RendVideo(YUVData_Frame *frame);
    int         m_yFrameLength=0;  // Y分量数据长度
    int         m_uFrameLength=0;  // U分量数据长度
    int         m_vFrameLength=0;  // V分量数据长度

protected:
    // 重写QOpenGLWidget方法，初始化OpenGL环境（如加载着色器、设置纹理等）
    void initializeGL();
    // 重写QOpenGLWidget方法，处理窗口大小变化时的OpenGL视口调整
    void resizeGL(int w, int h);
    // 重写QOpenGLWidget方法，执行OpenGL渲染绘制逻辑
    void paintGL();

private:
    // 初始化OpenGL着色器程序（包括顶点着色器和片段着色器的编译、链接）
    void initializeGLShaders();
    //GLuint createImageTextures(QString &pathString);

private:
    bool        m_bUpdateData = false;  // 数据更新标志，指示是否有新的视频数据需要渲染
    GLuint      m_textures[3];          // OpenGL纹理ID数组，分别对应Y、U、V三个颜色平面

    QOpenGLShaderProgram *m_pShaderProgram = NULL;  // OpenGL着色器程序对象，用于管理渲染着色器
    int         m_nVideoW    =0; // 视频帧宽度（分辨率宽）
    int         m_nVideoH    =0; // 视频帧高度（分辨率高）


    unsigned char* m_pBufYuv420p = NULL;// YUV420P格式视频数据缓冲区，用于共享数据（注意：不支持多线程渲染）
    // 顶点数据结构体，包含三维位置坐标和二维纹理坐标
    struct CCVertex{
        float x,y,z;  // 顶点三维位置坐标
        float u,v;    // 顶点二维纹理坐标
    };
};

#endif // CCOPENGLWIDGET_H

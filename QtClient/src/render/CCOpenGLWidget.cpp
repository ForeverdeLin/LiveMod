/**
 * @file CCOpenGLWidget.cpp
 * @brief OpenGL视频显示组件实现
 * @details 使用Qt OpenGL将YUV420P视频数据渲染到窗口
 *          知识点：Qt OpenGL、Shader着色器、YUV到RGB转换、纹理映射
 */

#include "CCOpenGLWidget.h"
#include <QDebug>
#include <QMouseEvent>

/**
 * @brief 构造函数
 * @details 初始化OpenGL视频显示组件
 * @param parent 父窗口指针
 */
CCOpenGLWidget::CCOpenGLWidget(QWidget *parent):QOpenGLWidget(parent)
{
    m_pBufYuv420p = NULL;      // YUV420P数据缓冲区
    m_pShaderProgram = NULL;   // Shader程序对象
    m_nVideoH = 0;             // 视频高度
    m_nVideoW = 0;             // 视频宽度
    m_yFrameLength =0;         // Y平面数据长度
    m_uFrameLength =0;         // U平面数据长度
    m_vFrameLength =0;         // V平面数据长度
}

CCOpenGLWidget::~CCOpenGLWidget()
{
    if(m_pShaderProgram != NULL){
        delete m_pShaderProgram;
        m_pShaderProgram = NULL;
    }
    if(NULL != m_pBufYuv420p)
    {
        free(m_pBufYuv420p);
        m_pBufYuv420p=NULL;
    }
    glDeleteTextures(3, m_textures);
}



/**
 * @brief 初始化OpenGL
 * @details 知识点：OpenGL初始化流程
 *          1. 初始化OpenGL函数（initializeOpenGLFunctions）
 *          2. 启用深度测试（glEnable GL_DEPTH_TEST）
 *          3. 设置背景颜色（glClearColor）
 *          4. 生成纹理（glGenTextures：3个纹理用于Y、U、V平面）
 *          5. 初始化Shader程序（initializeGLShaders）
 */
void CCOpenGLWidget::initializeGL()
{
    m_bUpdateData = false;  // 数据更新标志：未更新
    
    // 知识点：初始化OpenGL函数
    // initializeOpenGLFunctions - 初始化Qt封装的OpenGL函数指针
    // 必须在调用OpenGL函数前调用
    initializeOpenGLFunctions();
    
    // 知识点：启用深度测试
    // glEnable(GL_DEPTH_TEST) - 启用深度缓冲，用于3D渲染（这里主要用于初始化）
    glEnable(GL_DEPTH_TEST);
    
    // 知识点：设置背景颜色
    // glClearColor - 设置清除颜色缓冲区的颜色（黑色）
    glClearColor(0.0,0.0,0.0,1.0);

    // 知识点：生成纹理对象
    // glGenTextures - 生成纹理对象名称
    // 3个纹理：分别用于Y、U、V三个平面
    glGenTextures(3, m_textures);
    
    // 初始化Shader程序（用于YUV到RGB转换）
    initializeGLShaders();
    return;
}

/**
 * @brief 渲染视频帧
 * @details 知识点：YUV420P数据组织和OpenGL更新
 *          1. 检查分辨率变化，重新分配缓冲区
 *          2. 复制YUV数据到缓冲区（Y平面 + U平面 + V平面）
 *          3. 标记数据已更新
 *          4. 调用update()触发重绘（会调用paintGL）
 * @param yuvFrame YUV420P视频帧数据
 */
void CCOpenGLWidget::RendVideo(YUVData_Frame *yuvFrame)
{
    if(yuvFrame == NULL) {
        return;
    }

    // 知识点：检查分辨率变化
    // 如果分辨率改变，需要重新分配缓冲区
    if(m_nVideoH != yuvFrame->height || m_nVideoW != yuvFrame->width){
        if(NULL != m_pBufYuv420p)
        {
            free(m_pBufYuv420p);
            m_pBufYuv420p=NULL;
        }
    }
    
    // 更新视频尺寸
    m_nVideoW = yuvFrame->width;
    m_nVideoH = yuvFrame->height;

    // 获取各平面数据长度
    m_yFrameLength = yuvFrame->luma.length;      // Y平面长度
    m_uFrameLength = yuvFrame->chromaB.length;   // U平面长度
    m_vFrameLength = yuvFrame->chromaR.length;   // V平面长度

    // 知识点：计算YUV420P总大小
    // YUV420P格式：Y平面（完整分辨率）+ U平面（1/4分辨率）+ V平面（1/4分辨率）
    // 总大小 = width * height * 1.5
    int nLen = m_yFrameLength + m_uFrameLength + m_vFrameLength;

    // 知识点：分配YUV缓冲区
    // 如果缓冲区不存在，分配新缓冲区
    if(NULL == m_pBufYuv420p)
    {
        m_pBufYuv420p = (unsigned char*)malloc(nLen);
    }

    // 知识点：复制YUV数据到缓冲区
    // 数据排列：Y平面 + U平面 + V平面（平面格式）
    memcpy(m_pBufYuv420p,yuvFrame->luma.dataBuffer,m_yFrameLength);
    memcpy(m_pBufYuv420p+m_yFrameLength,yuvFrame->chromaB.dataBuffer,m_uFrameLength);
    memcpy(m_pBufYuv420p+m_yFrameLength + m_uFrameLength,yuvFrame->chromaR.dataBuffer,m_vFrameLength);

    // 知识点：标记数据已更新并触发重绘
    // update() - Qt的更新函数，会调用paintGL()进行实际渲染
    m_bUpdateData = true;
    update();
}

/**
 * @brief 初始化OpenGL Shader程序
 * @details 知识点：OpenGL Shader编程
 *          1. 创建顶点着色器（Vertex Shader）
 *          2. 编译顶点着色器（从资源文件加载）
 *          3. 创建片段着色器（Fragment Shader）
 *          4. 编译片段着色器（从资源文件加载）
 *          5. 创建Shader程序并链接
 *          
 *          Shader作用：
 *          - 顶点着色器：处理顶点位置和纹理坐标
 *          - 片段着色器：将YUV纹理转换为RGB颜色
 * @note Shader文件位于Qt资源文件（:/Shaders/）
 */
void CCOpenGLWidget::initializeGLShaders()
{
    // 知识点：创建并编译顶点着色器
    // QOpenGLShader - Qt封装的OpenGL着色器类
    // QOpenGLShader::Vertex - 顶点着色器类型
    QOpenGLShader* vertexShader = new QOpenGLShader(QOpenGLShader::Vertex,this);
    // compileSourceFile - 从资源文件编译着色器源码
    bool bCompileVS = vertexShader->compileSourceFile(":/Shaders/vertex.vert");
    if(bCompileVS == false){
        qDebug()<<"VS Compile ERROR:"<<vertexShader->log();
    }
    
    // 知识点：创建并编译片段着色器
    // QOpenGLShader::Fragment - 片段着色器类型
    QOpenGLShader* fragmentShader = new QOpenGLShader(QOpenGLShader::Fragment,this);
    // 片段着色器负责YUV到RGB的转换
    bool bCompileFS = fragmentShader->compileSourceFile(":/Shaders/fragment.frag");
    if(bCompileFS == false){
        qDebug()<<"FS Compile ERROR:"<<fragmentShader->log();
    }
    
    // 知识点：创建Shader程序并链接
    // QOpenGLShaderProgram - Qt封装的OpenGL着色器程序类，qrc资源文件里的着色器代码
    m_pShaderProgram = new QOpenGLShaderProgram();
    m_pShaderProgram->addShader(vertexShader);   // 添加顶点着色器
    m_pShaderProgram->addShader(fragmentShader); // 添加片段着色器
    bool linkStatus = m_pShaderProgram->link(); // 链接Shader程序
    if(linkStatus == false){
        qDebug()<<"LINK ERROR:"<<m_pShaderProgram->log();
    }
    if(vertexShader != NULL){
        delete vertexShader;
        vertexShader = NULL;
    }
    if(fragmentShader != NULL){
        delete fragmentShader;
        fragmentShader = NULL;
    }
}

void CCOpenGLWidget::resizeGL(int w, int h)
{
    glViewport(0,0, w,h);
}

void CCOpenGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    if(m_bUpdateData == false){
        return;
    }

    static CCVertex triangleVert[] = {
                                      {-1,  1, 0,  0, 0},
                                      {-1, -1, 0,  0, 1},//x,y,z,u,v
                                      {1,   1, 0,  1, 0},
                                      {1,  -1, 0,  1, 1},
                                      };

    QMatrix4x4 matrix;
    matrix.ortho(-1,1,-1,1,0.1,1000);//正交投影
    matrix.translate(0,0,-3);//平移

    m_pShaderProgram->bind();

    m_pShaderProgram->setUniformValue("uni_mat",matrix);

    m_pShaderProgram->enableAttributeArray("attr_position");
    m_pShaderProgram->enableAttributeArray("attr_uv");

    m_pShaderProgram->setAttributeArray("attr_position",GL_FLOAT,triangleVert,3,sizeof(CCVertex));
    m_pShaderProgram->setAttributeArray("attr_uv",GL_FLOAT,&triangleVert[0].u,2,sizeof(CCVertex));

    m_pShaderProgram->setUniformValue("uni_textureY",0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textures[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_nVideoW, m_nVideoH, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, m_pBufYuv420p);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


    m_pShaderProgram->setUniformValue("uni_textureU",1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textures[1]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_nVideoW/2, m_nVideoH/2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, (char*)(m_pBufYuv420p+m_yFrameLength));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_pShaderProgram->setUniformValue("uni_textureV",2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_textures[2]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_nVideoW/2, m_nVideoH/2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, (char*)(m_pBufYuv420p+m_yFrameLength+m_uFrameLength));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_pShaderProgram->disableAttributeArray("attr_position");
    m_pShaderProgram->disableAttributeArray("attr_uv");

    m_pShaderProgram->release();

    return;
}

/*GLuint CCOpenGLWidget::createImageTextures(QString &pathString)
{
    unsigned int textureId;
    glGenTextures(1,&textureId);//产生纹理索引
    glBindTexture(GL_TEXTURE_2D,textureId); //绑定纹理索引，之后的操作都针对当前纹理索引
    QImage texImage=QImage(pathString.toLocal8Bit().data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);//指当纹理图象被使用到一个大于它的形状上时
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);//指当纹理图象被使用到一个小于或等于它的形状上时
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,texImage.width(),texImage.height(),0,GL_RGBA,GL_UNSIGNED_BYTE,texImage.rgbSwapped().bits());//指定参数，生成纹理
    return textureId;
}
*/

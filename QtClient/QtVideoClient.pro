QT       += core gui
QT += opengl openglwidgets
QT += websockets  # WebSocket支持
QT += multimedia  # 音视频设备管理
QT += multimediawidgets  # 音频输出支持

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17


# mac{
#     DEFINES += CCVIDEOCLIENT_MACOSX

#     QMAKE_LFLAGS    += -framework QuartzCore
#     QMAKE_LFLAGS    += -framework Foundation
#     QMAKE_LFLAGS    += -framework CoreMedia
#     QMAKE_LFLAGS    += -framework AudioUnit
#     QMAKE_LFLAGS    += -framework AVFoundation
#     QMAKE_LFLAGS    += -framework VideoToolbox
#     QMAKE_LFLAGS    += -framework VideoDecodeAcceleration
#     QMAKE_LFLAGS    += -framework CoreGraphics
#     QMAKE_LFLAGS    += -framework AppKit
#     QMAKE_LFLAGS    += -framework ForceFeedback
#     QMAKE_LFLAGS    += -framework Carbon
#     QMAKE_LFLAGS    += -framework AudioToolbox
#     QMAKE_LFLAGS    += -framework CoreAudio



LIBS += -lX11


#     INCLUDEPATH += /usr/local/include
#     LIBS += -L/usr/local/lib -lz -lbz2 -llzma -liconv
# }
# win32
# {
#     DEFINES += CCVIDEOCLIENT_WIN32

# }

INCLUDEPATH += $$PWD/3rdParty/mac/libffmpeg/include#将当前 .pro 文件所在目录下的 3rdParty/mac/libffmpeg/include 路径添加到项目的头文件搜索路径中
LIBS += -L$$PWD/3rdParty/mac/libFFmpeg/lib -lavformat -lavcodec -lavutil -lswresample -lswscale -lavdevice -lavfilter

#INCLUDEPATH += /usr/local/ffmpeg/include
#LIBS += -L/usr/local/ffmpeg/lib \
        # -lavutil \
        # -lavcodec \
        # -lavformat \
        # -lavdevice \
        # -lswscale\
        # -lavfilter\
        # -lswresample


# 编译器定义
DEFINES += QT_DEPRECATED_WARNINGS


SOURCES += \
    CCOpenGLWidget.cpp \
    CCVideoClient.cpp \
    h264decoder.cpp \
    aacdecoder.cpp \
    main.cpp \
    mainwindow.cpp \
    WebSocketControlClient.cpp

HEADERS += \
    CCOpenGLWidget.h \
    CCSocketDefine.h \
    CCVideoClient.h \
    CCYUVDataDefine.h \
    h264decoder.h \
    aacdecoder.h \
    mainwindow.h \
    WebSocketControlClient.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    videoclient.qrc



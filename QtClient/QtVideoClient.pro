QT       += core gui
QT += opengl openglwidgets
QT += websockets
QT += multimedia
QT += multimediawidgets

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
#     LIBS += -lX11
#     INCLUDEPATH += /usr/local/include
#     LIBS += -L/usr/local/lib -lz -lbz2 -llzma -liconv
# }
# win32
# {
#     DEFINES += CCVIDEOCLIENT_WIN32
# }

INCLUDEPATH += $$PWD/3rdParty/mac/libffmpeg/include
INCLUDEPATH += $$PWD/../common/include
INCLUDEPATH += $$PWD/include
INCLUDEPATH += $$PWD/src/core
INCLUDEPATH += $$PWD/src/decoder
INCLUDEPATH += $$PWD/src/render
INCLUDEPATH += $$PWD/src/network

LIBS += -L$$PWD/3rdParty/mac/libFFmpeg/lib \
    -lavformat -lavcodec -lavutil -lswresample -lswscale -lavdevice -lavfilter

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/core/main.cpp \
    src/core/mainwindow.cpp \
    src/decoder/h264decoder.cpp \
    src/decoder/aacdecoder.cpp \
    src/render/CCOpenGLWidget.cpp \
    src/network/CCVideoClient.cpp \
    src/network/WebSocketControlClient.cpp

HEADERS += \
    src/core/mainwindow.h \
    src/decoder/h264decoder.h \
    src/decoder/aacdecoder.h \
    src/render/CCOpenGLWidget.h \
    src/network/CCVideoClient.h \
    src/network/WebSocketControlClient.h \
    include/CCSocketDefine.h \
    include/CCYUVDataDefine.h

FORMS += \
    src/core/mainwindow.ui

RESOURCES += \
    resources/videoclient.qrc

#-------------------------------------------------
#
# Project created by QtCreator 2020-08-01T00:34:59
#
#-------------------------------------------------

QT       += core gui opengl widgets openglwidgets

TARGET = VideoWidget
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11
include(VideoWidget/videowidget.pri)
SOURCES += \
        main.cpp \
        widget.cpp

HEADERS += \
        widget.h

INCLUDEPATH += E:/Vcpkg/packages/ffmpeg_x64-windows/include \
               $$(INTELMEDIASDKROOT)/include \
               "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.1/include"

LIBS += -LE:/Vcpkg/packages/ffmpeg_x64-windows/lib \
        -L"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.1/lib/x64" \
        avcodec.lib avdevice.lib avfilter.lib avformat.lib avutil.lib postproc.lib swresample.lib swscale.lib cuda.lib

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

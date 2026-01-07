QT       += core gui multimedia multimediawidgets serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    drv_camera.cpp \
    main.cpp \
    pro_hidcontroller.cpp \
    drv_ch9329.cpp \
    pro_videothread.cpp \
    ui_display.cpp \
    ui_mainpage.cpp

HEADERS += \
    drv_camera.h \
    pro_hidcontroller.h \
    drv_ch9329.h\
    pro_videothread.h \
    ui_display.h \
    ui_mainpage.h

FORMS += \
    ui_mainpage.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# ElaWidgetTools 配置
INCLUDEPATH += $$PWD/SDK/ElaWidgetTools/include
DEPENDPATH  += $$PWD/SDK/ElaWidgetTools/include

# 链接库 (-L指定路径, -l指定库名去头去尾)
LIBS += -L$$PWD/SDK/ElaWidgetTools/lib -lElaWidgetTools

# 确保运行时能找到库 (开发阶段)
QMAKE_RPATHDIR += $$PWD/SDK/ElaWidgetTools/lib

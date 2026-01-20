QT       += core gui multimedia multimediawidgets serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp                        \
    Driver/drv_webserver.cpp        \
    Driver/drv_camera.cpp           \
    Driver/drv_ch9329.cpp           \
    Controller/pro_hidcontroller.cpp\
    Controller/pro_videothread.cpp  \
    QtUiPage/ui_display.cpp         \
    QtUiPage/ui_mainpage.cpp        \
    Tool/videoencoder.cpp

HEADERS += \
    Driver/drv_camera.h           \
    Driver/drv_ch9329.h           \
    Driver/drv_webserver.h        \
    Controller/pro_hidcontroller.h\
    Controller/pro_videothread.h  \
    QtUiPage/ui_display.h         \
    QtUiPage/ui_mainpage.h        \
    Tool/videoencoder.h           \
    Tool/safe_queue.h

FORMS += QtUiPage/ui_mainpage.ui

RESOURCES += webpages.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 引入 FFmpeg库
LIBS += -lavcodec  -lavutil -lswscale
# 引入 OpenSSL库
LIBS += -lcrypto

# ElaWidgetTools 配置
INCLUDEPATH += $$PWD/SDK/ElaWidgetTools/include
DEPENDPATH  += $$PWD/SDK/ElaWidgetTools/include

# 链接动态库 (-L指定路径, -l指定库名去头去尾)
 LIBS += -L$$PWD/SDK/ElaWidgetTools/lib/x86 -lElaWidgetTools
# LIBS += -L$$PWD/SDK/ElaWidgetTools/lib/arm -lElaWidgetTools

# 确保运行时能找到库 (开发阶段)
 QMAKE_RPATHDIR += $$PWD/SDK/ElaWidgetTools/lib/x86
# QMAKE_RPATHDIR += $$PWD/SDK/ElaWidgetTools/lib/arm

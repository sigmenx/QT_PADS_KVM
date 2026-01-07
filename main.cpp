#include "ui_mainpage.h"

#include <QApplication>

#include "ElaApplication.h" // 引入头文件

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // === 初始化 ElaWidgetTools ===
    ElaApplication::getInstance()->init();
    // 你可以在这里设置初始主题，例如深色模式
    //ElaApplication::getInstance()->setThemeMode(ElaThemeType::Dark);
    // ===========================

    ui_mainpage w;
    w.show();
    return a.exec();
}

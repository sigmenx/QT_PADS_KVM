#include "QtUiPage/ui_mainpage.h"

#include <QApplication>

#include "ElaApplication.h" // 引入头文件

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // === 初始化 ElaWidgetTools ===
    ElaApplication::getInstance()->init();

    ui_mainpage w;
    w.show();
    return a.exec();
}

#ifndef UI_MAINPAGE_H
#define UI_MAINPAGE_H

#include <QWidget>
#include <QList>
#include <QStandardItemModel> // <--- 新增引用

// 前置声明，减少头文件依赖，加快编译
class QTableWidgetItem;
class QLabel;

namespace Ui {
class ui_mainpage;
}

class ui_mainpage : public QWidget
{
    Q_OBJECT

public:
    explicit ui_mainpage(QWidget *parent = nullptr);
    ~ui_mainpage();

private slots:
    // 自动连接的槽函数
    void on_btnRefresh_clicked();
    //void on_cameraTable_itemClicked(QTableWidgetItem *item);
    void on_btnOpen_clicked();
    void on_btnOpenSerialHID_clicked();

private:
    Ui::ui_mainpage *ui;

    // === 新增：数据模型 ===
    QStandardItemModel *m_cameraModel;

    // --- 初始化相关 ---
    void initUI();         // 初始化界面控件

    // --- 业务逻辑辅助 ---
    void refreshCameraList();
    void refreshSerialList();

    // 辅助：更新Label状态的通用函数
    void updateStatusLabel(QLabel *label, bool isSuccess, const QString &Text);

    // --- 硬件检测 ---
    // 检测摄像头状态 (返回状态描述字符串)
    QString checkCameraStatus(const QString &devicePath);

};

#endif // UI_MAINPAGE_H

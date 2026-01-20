#ifndef UI_MAINPAGE_H
#define UI_MAINPAGE_H

#include <QWidget>
#include <QStandardItemModel>
#include <QLabel>

namespace Ui {
class ui_mainpage;
}

class ui_mainpage : public QWidget
{
    Q_OBJECT

public:
    explicit ui_mainpage(QWidget *parent = nullptr);
    ~ui_mainpage();

private:
    Ui::ui_mainpage *ui;

    // === 新增：数据模型 ===
    QStandardItemModel *m_cameraModel;

    // --- 初始化相关 ---
    void initUI();         // 初始化界面控件

    // --- 业务逻辑 ---
    void refreshCameraList();
    void refreshSerialList();

    // 检测摄像头状态 (返回状态描述字符串)
    QString checkCameraStatus(const QString &devicePath);

    // 辅助函数：更新Label状态
    void updateStatusLabel(QLabel *label, const QString &Text, const QString &Color);

private slots:
    // 自动连接的槽函数
    void on_btnOpenSerialHID_clicked();
    void on_btnOpen_clicked();
    void on_btnRefresh_clicked();

};

#endif // UI_MAINPAGE_H

#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <QWidget>
#include <QTimer>
#include <QLabel>
#include <QBoxLayout>
#include <QGroupBox>
#include <QResizeEvent>
#include <QThread>

// 引入 Ela 控件
// #include "ElaWidget.h" // 不再需要 ElaWidget
#include "ElaIconButton.h"
#include "ElaPushButton.h"
#include "ElaComboBox.h"
#include "ElaRadioButton.h"

// 引入新的线程头文件
#include "pro_videothread.h"
#include "pro_hidcontroller.h"

// 修改为继承 QWidget
class ui_display : public QWidget
{
    Q_OBJECT

public:
    explicit ui_display(QString camdevPath, QWidget *parent = nullptr);
    ~ui_display();

private:
    void initUI();          // 总初始化
    void startCameraLogic();
    void startSerialLogic();
    void initTopBar();      // 初始化顶部
    void initCenter(QWidget *centerContainer); // 初始化中间区域
    void initSideBar();     // 初始化侧边栏


    // === 通用的下拉框刷新模板函数 ===
    // 参数：目标下拉框、数据列表、如何从数据中获取显示文本的Lambda函数
    template <typename T>
    void updateComboBox(ElaComboBox* box, const QList<T>& list, std::function<QString(const T&)> textFunc) {
        if (!box) return;
        box->blockSignals(true); // 暂停信号
        box->clear();
        for (const auto& item : list) {
            // 存入 item 数据 (QVariant(item)) 供后续提取，同时设置显示文本
            box->addItem(textFunc(item), QVariant::fromValue(item));
        }
        box->blockSignals(false); // 恢复信号
        // 如果有数据，默认选中第一个
        if (box->count() > 0) {
            box->setCurrentIndex(0);
        }
    }


private slots:
    // --- 业务逻辑槽函数 ---
    void handleFrame(QImage image);     //接收子线程图像

    void on_btn_vid_StrOn_clicked();    // 暂停/启动
    void on_btn_vid_FullScr_clicked();  // 全屏切换
    void on_btn_vid_PicCap_clicked();   // 截图
    void on_btn_vid_SetApply_clicked(); // 应用视频设置
    void on_btn_hid_SetApply_clicked(); // 串口应用设置

    // --- 界面交互槽函数 ---
    void on_btn_ui_HideTop_clicked();   // 隐藏顶部
    void on_btn_ui_HideSide_clicked();  // 隐藏侧边

    void on_cmb_vid_FmtSelect_currentIndexChanged(int index);
    void on_cmb_vid_ResSelect_currentIndexChanged(int index);

private:
    // --- 核心数据 ---
    QString m_camdevPath;

    VideoController *m_VideoManager;
    HidController *m_HidManager;

    bool m_topbarvisiable;

    // --- 界面控件成员变量 ---

    // 1. 顶部区域
    QWidget *m_topWidget;
    ElaIconButton *btn_ui_HideTop;

    // 1.1 HID控制部分
    QLabel *lbl_vid_HIDStatus;
    ElaRadioButton *rbt_hid_EnCtrl;
    ElaRadioButton *rbt_hid_AbsMode;
    ElaRadioButton *rbt_hid_RefMode;

    ElaComboBox *cmb_hid_MutKeySel;
    ElaComboBox *cmb_hid_MedKeySel;
    ElaPushButton *btn_hid_KeySend;

    // 1.2 视频控制部分
    ElaIconButton *btn_vid_FullScr;
    ElaIconButton *btn_vid_StrOn;
    ElaIconButton *btn_vid_PicCap;

    // 1.3 音频控制部分
    ElaIconButton *btn_aud_OnOff;

    // 2. 中间视频区域
    QWidget *m_videoContainer;
    QLabel *lbl_ui_VideoShow;
    ElaIconButton *btn_ui_HideSide; // 悬浮按钮

    // 3. 右侧参数设置区
    QWidget *m_sideBarWidget;

    // 3.1 视频设置
    ElaComboBox *cmb_vid_FmtSelect;
    ElaComboBox *cmb_vid_ResSelect;
    ElaComboBox *cmb_vid_FpsSelect;
    ElaPushButton *btn_vid_SetApply;

    // 3.2 HID设置
    ElaComboBox *cmb_hid_UartSelect;
    ElaComboBox *cmb_hid_BtrSelect;
    ElaComboBox *cmb_hid_DevSelect;
    ElaPushButton *btn_hid_SetApply;

//窗口关闭
signals:
    void windowClosed(); // 定义关闭信号
protected:
    void closeEvent(QCloseEvent *event) override; // 重写关闭事件
};

#endif // UI_DISPLAY_H

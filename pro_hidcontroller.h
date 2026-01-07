#ifndef PRO_HIDCONTROLLER_H
#define PRO_HIDCONTROLLER_H

#include <QObject>
#include <QMap>
#include <QPoint>
#include <QTime>
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer> // 使用 QElapsedTimer 更精准
#include "drv_ch9329.h"

// === 核心修正：必须包含以下事件头文件 ===
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>

// 定义操作模式
enum HidControlMode {
    MODE_NONE,      // 不控制
    MODE_ABSOLUTE,  // 绝对坐标 (点击映射)
    MODE_RELATIVE   // 相对坐标 (触控拖拽)
};

class HidController : public QObject
{
    Q_OBJECT
public:
    explicit HidController(QObject *parent = nullptr);
    ~HidController();

    // === 辅助函数（对外接口） ===
    bool initDriver(const QString &portName, int baud);
    // 设置鼠标控制模式
    void setControlMode(HidControlMode mode);
    // 设置视频源分辨率 (在初始时调用一次即可)
    void setSourceResolution(const QSize &videoSize, const QSize &widgetSize);
    // 重新计算HID边界参数
    void updateScaleParams();

protected:
    // === 核心：事件过滤器 ===
    bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
    // 长按超时槽函数
    void onLongPressTimeout();

private:
    //鼠标相关
    void processMouse(QObject *watched, QEvent *e, QEvent::Type type);
    //键盘相关
    void processKey(QKeyEvent *e, bool isPress);
    uint8_t qtModifiersToHid(Qt::KeyboardModifiers modifiers);
    void initKeyMap();

private:

    // === 成员变量 ===
    CH9329Driver *m_driver;
    HidControlMode m_currentMode;
    QMap<int, uint8_t> m_keyMap;

    // === 缓存的几何参数 ===
    QSize m_sourceSize;  // 视频原始尺寸 (如 1920x1080)
    QSize m_widgetSize;  // 播放控件尺寸 (如 800x600)

    // === 预计算的映射参数 (避免每次鼠标移动都做除法) ===
    QRect m_displayRect; // 视频实际显示的区域 (去掉了黑边)

    // === 相对模式/触控逻辑专用变量 ===          //TODO：简化下面这一堆
    QPoint m_lastMousePos;    // 上次坐标
    QPoint m_pressGlobalPos;  // 按下时的坐标（用于计算总位移，判断是点击还是滑动）
    bool m_hasMoved;          // 是否发生了有效位移
    bool m_longPressTriggered;// 是否已经触发了长按
    QTimer *m_longPressTimer; // 长按定时器

    // === 丢弃事件 ===
    QElapsedTimer m_elapsedTimer; // 计时器
    qint64 m_lastSendTime;        // 上次发送的时间戳
    // 相对模式累积量 (依然需要，防止丢弃移动距离)
    int m_accumRelDx;
    int m_accumRelDy;
};

#endif // PRO_HIDCONTROLLER_H

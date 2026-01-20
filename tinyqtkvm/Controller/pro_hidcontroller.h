#ifndef PRO_HIDCONTROLLER_H
#define PRO_HIDCONTROLLER_H

#include "../Driver/drv_ch9329.h"
#include "../Tool/safe_queue.h"

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer> // 使用 QElapsedTimer 更精准
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
    // 统一的主循环 (10ms)
    void onMainLoop();

private:
    //本地鼠标事件
    void parseLocalMouse(QObject *watched, QEvent *evt);
    //本地键盘事件
    void parseLocalKey(QKeyEvent *e, bool isPress);


    uint8_t qtModifiersToHid(Qt::KeyboardModifiers modifiers);

    void initKeyMap();

private:
    // 一个轮询定时器
    QTimer *m_mainLoopTimer;

    // === 成员变量 ===
    CH9329Driver *m_driver;
    HidControlMode m_currentMode;
    QMap<int, uint8_t> m_keyMap;

    // === 缓存的几何参数 ===
    QSize m_sourceSize;  // 视频原始尺寸 (如 1920x1080)
    QSize m_widgetSize;  // 播放控件尺寸 (如 800x600)

    // === 预计算的映射参数 (避免每次鼠标移动都做除法) ===
    QRect m_displayRect; // 视频实际显示的区域 (去掉了黑边)

    // === 长按与触控逻辑变量 ===
    bool m_isLeftButtonDown;      // 左键是否按下
    qint64 m_pressStartTime;      // 按下时的时间戳
    QPoint m_pressStartPos;       // 按下时的坐标
    bool m_longPressHandled;      // 长按是否已经处理过
    bool m_hasMovedSignificantly; // 是否发生了明显移动

    // === 用于鼠标移动限流 ===
    qint64 m_lastMouseMoveTime; // 上一次处理MouseMove的时间戳

    // 全局计时器
    QElapsedTimer m_elapsedTimer;
};

#endif // PRO_HIDCONTROLLER_H

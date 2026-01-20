#include "pro_hidcontroller.h"
#include <QDebug>

// ===CH9329通信手册的宏定义 ===
#define MOUSE_LEFT      0x01
#define MOUSE_RIGHT     0x02
#define MOUSE_MIDDLE    0x04

#define MOD_NONE        0x00
#define MOD_L_CTRL      0x01
#define MOD_L_SHIFT     0x02
#define MOD_L_ALT       0x04
#define MOD_L_WIN       0x08
#define MOD_R_CTRL      0x10
#define MOD_R_SHIFT     0x20
#define MOD_R_ALT       0x40
#define MOD_R_WIN       0x80
// ==========================

// 静态辅助函数：将 Qt 的鼠标按键状态转换为 CH9329 的按键 Byte
static uint8_t getHidButtonState(Qt::MouseButtons buttons)// 这里使用 buttons() 而不是 button()，因为需要获取"当前所有按下的键"的状态
{
    uint8_t hidBtns = 0x00;
    if (buttons & Qt::LeftButton)   hidBtns |= 0x01; // MOUSE_LEFT
    if (buttons & Qt::RightButton)  hidBtns |= 0x02; // MOUSE_RIGHT
    if (buttons & Qt::MiddleButton) hidBtns |= 0x04; // MOUSE_MIDDLE
    return hidBtns;
}

HidController::HidController(QObject *parent): QObject(parent),
    m_driver(new CH9329Driver()),m_sourceSize(1920, 1080)
{
    //初始化键值映射表
    initKeyMap();

    //初始化鼠标模式
    m_currentMode = MODE_NONE;

    // 初始化状态变量
    m_isLeftButtonDown = false;
    m_longPressHandled = false;
    m_hasMovedSignificantly = false;
    m_lastMouseMoveTime = 0;
    m_elapsedTimer.start();

    // 统一的主循环定时器
    m_mainLoopTimer = new QTimer(this);
    m_mainLoopTimer->setInterval(10); // 10ms 间隔 (100Hz)，保证鼠标流畅
    connect(m_mainLoopTimer, &QTimer::timeout, this, &HidController::onMainLoop);
    m_mainLoopTimer->start();

}

HidController::~HidController() {
    if (m_driver) delete m_driver; //在m_driver里会关闭串口
}

// ==========================================
// 控制函数部分（供外部调用）
// ==========================================
// 初始化串口
bool HidController::initDriver(const QString &portName, int baud)
{
    if(m_driver->init(portName, baud)){
        return m_driver->checkConnection();
    }
    return false ;
}

// 切换鼠标模式
void HidController::setControlMode(HidControlMode mode) {
    m_currentMode = mode;
}

// 辅助函数：更新视频源分辨率
void HidController::setSourceResolution(const QSize &videoSize, const QSize &widgetSize)
{
    // 1. 更新源分辨率
    m_sourceSize = videoSize;
    // 2. 更新控件尺寸
    m_widgetSize = widgetSize;
    // 3. 立即触发参数重算
    updateScaleParams();
}

// 辅助函数：预计算显示区域 (复刻 Qt::KeepAspectRatio 算法)
void HidController::updateScaleParams()
{
    if (m_sourceSize.isEmpty() || m_widgetSize.isEmpty()) return;

    // 1. 计算缩放后的尺寸
    // 使用 Qt 内置的 scaled 函数逻辑计算尺寸，确保与 handleFrame 的显示一致
    // 这里的 scaled 只是算数，不处理图片，非常快
    QSize scaledSize = m_sourceSize.scaled(m_widgetSize, Qt::KeepAspectRatio);

    // 2. 计算偏移量 (居中显示)
    int x = (m_widgetSize.width() - scaledSize.width()) / 2;
    int y = (m_widgetSize.height() - scaledSize.height()) / 2;

    // 3. 存入缓存
    m_displayRect = QRect(x, y, scaledSize.width(), scaledSize.height());

    qDebug() << "Scale Update: Source" << m_sourceSize
             << "Widget" << m_widgetSize
             << "DisplayRect" << m_displayRect;
}

// ==========================================
// 统一主循环 (负责：取队列发送 + 长按检测)
// ==========================================
void HidController::onMainLoop()
{
    // 1. 处理队列中的所有指令 (尽可能清空，防止延迟堆积)
    HidCommand cmd;
    while (HidPacketQueue::instance()->pop(cmd)) {
        if (!m_driver) continue;

        if (cmd.type == HidCommand::CMD_MOUSE_ABS) {
            m_driver->sendMouseAbs(cmd.param1, cmd.param2, cmd.param3, cmd.param4);
            qDebug()<<"ABSmode";
            qDebug()<<"x:"<<cmd.param1<<",y:"<<cmd.param2<<",button:"<<cmd.param3<<",wheel:"<<cmd.param4;
        }
        else if (cmd.type == HidCommand::CMD_MOUSE_REL) {
            m_driver->sendMouseRel(cmd.param1, cmd.param2, cmd.param3, cmd.param4);
            qDebug()<<"RELmode";
            qDebug()<<"x:"<<cmd.param1<<",y:"<<cmd.param2<<",button:"<<cmd.param3<<",wheel:"<<cmd.param4;
        }
        else if (cmd.type == HidCommand::CMD_KEYBOARD) {
            qDebug()<<"KEYBOD";
            qDebug()<<"modifiers:"<<cmd.param1<<",key:"<<cmd.param2;
            m_driver->sendKbPacket(cmd.param1, cmd.param2);
        }
    }

    // 2. 长按检测逻辑 (在 Loop 中轮询)
    // 仅在 相对模式(触控) 下生效
    if (m_currentMode == MODE_RELATIVE && m_isLeftButtonDown && !m_longPressHandled) {
        // 如果移动距离超过阈值，取消长按判定
        if (m_hasMovedSignificantly) {
            m_longPressHandled = true; // 视为无效，不再检测长按
        }
        else {
            // 检查时间差 (500ms)
            if (m_elapsedTimer.elapsed() - m_pressStartTime > 500) {
                qDebug() << "Touch: Long Press Triggered (Right Click)";
                m_driver->clickMouse(MOUSE_RIGHT); // 发送右键点击
                m_longPressHandled = true;         // 标记已处理
            }
        }
    }
}

// ==========================================
// 事件过滤器 (负责：解析本地事件 -> Push 队列)
// ==========================================
bool HidController::eventFilter(QObject *watched, QEvent *event) {
    if (m_currentMode == MODE_NONE || !m_driver) {
        return QObject::eventFilter(watched, event);
    }

    // 处理 Resize
    // 只有当"被监视对象"是 QLabel (视频显示控件) 时，才更新尺寸。
    // 过滤掉主窗口(ui_display)的 Resize 事件，防止将包含侧边栏的窗口尺寸误判为视频区域尺寸。
    if (event->type() == QEvent::Resize && watched->inherits("QLabel")) {
        m_widgetSize = static_cast<QWidget*>(watched)->size();
        updateScaleParams();
        return false; // 不拦截 Resize，让控件自己也能处理布局
    }

    // 解析并 Push
    switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            parseLocalKey(static_cast<QKeyEvent*>(event), event->type() == QEvent::KeyPress);
            return true; // 拦截
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove:
        case QEvent::Wheel:
            if (watched->isWidgetType()) {
                parseLocalMouse(watched, event);//, event->type());
                return true; // 拦截
            }
            break;
        default: break;
    }
    return QObject::eventFilter(watched, event);
}

// 本地鼠标解析
void HidController::parseLocalMouse(QObject *watched, QEvent *evt)
{
    // === 1. 优先处理滚轮事件 (强制走相对模式) ===
    if (evt->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent*>(evt);

        // 计算滚轮步进
        int delta = we->angleDelta().y();
        int8_t wheelByte = 0;
        if (delta > 0) wheelByte = 1;
        else if (delta < 0) wheelByte = -1;

        if (wheelByte != 0) {
            HidCommand cmd;
            cmd.type = HidCommand::CMD_MOUSE_REL; // 强制使用 REL 模式
            cmd.param1 = 0; // xRel
            cmd.param2 = 0; // yRel
            cmd.param3 = 0; // buttons
            cmd.param4 = wheelByte;
            HidPacketQueue::instance()->push(cmd);
        }
        // 滚轮处理完毕，直接返回，不走下面的坐标计算逻辑
        return;
    }else if (evt->type() == QEvent::MouseMove) {
        // === 鼠标移动限流逻辑 === 50Hz采样降频 === 仅针对 MouseMove
        qint64 now = m_elapsedTimer.elapsed();
        if (now - m_lastMouseMoveTime < 20) {
            return; // 距离上次发送不足 20ms，直接丢弃该包
        }
        m_lastMouseMoveTime = now; // 更新发送时间
    }


    // === 2. 处理普通鼠标事件 (按键/移动) ===
    // 此时肯定是 MouseButtonPress / Release / Move
    QMouseEvent *me = static_cast<QMouseEvent*>(evt);
    // [QT5] 直接使用 pos()
    QPoint currentPos = me->pos();
    Qt::MouseButtons buttons = me->buttons();

    // === 3. 绝对模式处理 ===
    if (m_currentMode == MODE_ABSOLUTE) {
        if (m_displayRect.isEmpty()) return;

        // 计算 0-4095 坐标 (基于 currentPos，无论是移动还是滚轮都适用)
        int realX = currentPos.x() - m_displayRect.x();
        int realY = currentPos.y() - m_displayRect.y();

        int hidX = 0, hidY = 0;
        if (m_displayRect.width() > 0)
            hidX = (int)((long long)qBound(0, realX, m_displayRect.width()) * 4095 / m_displayRect.width());
        if (m_displayRect.height() > 0)
            hidY = (int)((long long)qBound(0, realY, m_displayRect.height()) * 4095 / m_displayRect.height());

        // 构造命令 Push
        HidCommand cmd;
        cmd.type = HidCommand::CMD_MOUSE_ABS;
        cmd.param1 = hidX;
        cmd.param2 = hidY;
        cmd.param3 = getHidButtonState(buttons);
        cmd.param4 = 0; // 移动事件不带滚轮 (滚轮已在上方独立处理)
        HidPacketQueue::instance()->push(cmd);
    }

    // === 4. 相对模式处理 ===
    else if (m_currentMode == MODE_RELATIVE) {
        QEvent::Type type = evt->type();
        if (type == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
            m_isLeftButtonDown = true;
            m_pressStartTime = m_elapsedTimer.elapsed();
            // [QT5] 使用 globalPos()
            m_pressStartPos = me->globalPos();
            m_longPressHandled = false;
            m_hasMovedSignificantly = false;
        }
        else if (type == QEvent::MouseMove) {
            // [QT5] 使用 globalPos() 计算差值
            static QPoint lastPos = me->globalPos();
            int dx = me->globalPos().x() - lastPos.x();
            int dy = me->globalPos().y() - lastPos.y();
            lastPos = me->globalPos();

            // 判断是否大幅移动
            if ((me->globalPos() - m_pressStartPos).manhattanLength() > 5) {
                m_hasMovedSignificantly = true;
            }

            HidCommand cmd;
            cmd.type = HidCommand::CMD_MOUSE_REL;
            cmd.param1 = qBound(-127, dx, 127);
            cmd.param2 = qBound(-127, dy, 127);
            cmd.param3 = 0;
            cmd.param4 = 0;
            HidPacketQueue::instance()->push(cmd);
        }
        else if (type == QEvent::MouseButtonRelease && me->button() == Qt::LeftButton) {
            m_isLeftButtonDown = false;
            if (!m_longPressHandled && !m_hasMovedSignificantly) {
                HidCommand clickDown = {HidCommand::CMD_MOUSE_REL, 0, 0, MOUSE_LEFT, 0};
                HidCommand clickUp   = {HidCommand::CMD_MOUSE_REL, 0, 0, 0, 0};
                HidPacketQueue::instance()->push(clickDown);
                HidPacketQueue::instance()->push(clickUp);
            }
        }
    }
}

// 本地键盘解析
void HidController::parseLocalKey(QKeyEvent *e, bool isPress)
{
    if (e->isAutoRepeat()) return;

    // 查表逻辑 (这里复用之前的 m_keyMap)
    int key = e->key();
    uint8_t mods = qtModifiersToHid(e->modifiers());
    uint8_t hidCode = 0;

    if (m_keyMap.contains(key)) {
        hidCode = m_keyMap[key];
    }

    // 构造命令 Push (isPress 决定是否发送 0)
    HidCommand cmd;
    cmd.type = HidCommand::CMD_KEYBOARD;
    cmd.param1 = mods;
    cmd.param2 = isPress ? hidCode : 0x00;

    HidPacketQueue::instance()->push(cmd);
}


// === 修饰符转换 ===
uint8_t HidController::qtModifiersToHid(Qt::KeyboardModifiers modifiers) {
    uint8_t hidMod = 0;
    if (modifiers & Qt::ControlModifier) hidMod |= MOD_L_CTRL;
    if (modifiers & Qt::ShiftModifier)   hidMod |= MOD_L_SHIFT;
    if (modifiers & Qt::AltModifier)     hidMod |= MOD_L_ALT;
    if (modifiers & Qt::MetaModifier)    hidMod |= MOD_L_WIN; // Win键
    return hidMod;
}

// === 扩充后的键值映射表 ===
void HidController::initKeyMap() {
    // 字母 A-Z (Qt::Key_A 对应 0x04)
    // 注意：Qt::Key_A 无论按下 'a' 还是 'A' 都是同一个值，Shift 由 modifiers 处理
    for (int i = 0; i < 26; ++i) {
        m_keyMap[Qt::Key_A + i] = 0x04 + i;
    }

    // 数字 1-0 (主键盘区)
    // 0x1E ~ 0x27
    m_keyMap[Qt::Key_1] = 0x1E; m_keyMap[Qt::Key_Exclam] = 0x1E; // 1 和 ! 是同一个键
    m_keyMap[Qt::Key_2] = 0x1F; m_keyMap[Qt::Key_At] = 0x1F;     // 2 和 @
    m_keyMap[Qt::Key_3] = 0x20; m_keyMap[Qt::Key_NumberSign] = 0x20; // 3 和 #
    m_keyMap[Qt::Key_4] = 0x21; m_keyMap[Qt::Key_Dollar] = 0x21; // 4 和 $
    m_keyMap[Qt::Key_5] = 0x22; m_keyMap[Qt::Key_Percent] = 0x22; // 5 和 %
    m_keyMap[Qt::Key_6] = 0x23; m_keyMap[Qt::Key_AsciiCircum] = 0x23; // 6 和 ^
    m_keyMap[Qt::Key_7] = 0x24; m_keyMap[Qt::Key_Ampersand] = 0x24; // 7 和 &
    m_keyMap[Qt::Key_8] = 0x25; m_keyMap[Qt::Key_Asterisk] = 0x25; // 8 和 *
    m_keyMap[Qt::Key_9] = 0x26; m_keyMap[Qt::Key_ParenLeft] = 0x26; // 9 和 (
    m_keyMap[Qt::Key_0] = 0x27; m_keyMap[Qt::Key_ParenRight] = 0x27; // 0 和 )

    // 功能键
    m_keyMap[Qt::Key_Return]    = 0x28; // Enter
    m_keyMap[Qt::Key_Enter]     = 0x28; // NumPad Enter 有时也是这个，或者 0x58
    m_keyMap[Qt::Key_Escape]    = 0x29;
    m_keyMap[Qt::Key_Backspace] = 0x2A;
    m_keyMap[Qt::Key_Tab]       = 0x2B;
    m_keyMap[Qt::Key_Space]     = 0x2C;

    // 符号键 (关键部分：映射 Qt 的标点到 HID 键码)
    // 减号 - 和 下划线 _
    m_keyMap[Qt::Key_Minus] = 0x2D; m_keyMap[Qt::Key_Underscore] = 0x2D;
    // 等号 = 和 加号 +
    m_keyMap[Qt::Key_Equal] = 0x2E; m_keyMap[Qt::Key_Plus] = 0x2E;
    // 左中括号 [ 和 左大括号 {
    m_keyMap[Qt::Key_BracketLeft] = 0x2F; m_keyMap[Qt::Key_BraceLeft] = 0x2F;
    // 右中括号 ] 和 右大括号 }
    m_keyMap[Qt::Key_BracketRight] = 0x30; m_keyMap[Qt::Key_BraceRight] = 0x30;
    // 反斜杠 \ 和 竖线 |
    m_keyMap[Qt::Key_Backslash] = 0x31; m_keyMap[Qt::Key_Bar] = 0x31;
    // 分号 ; 和 冒号 :
    m_keyMap[Qt::Key_Semicolon] = 0x33; m_keyMap[Qt::Key_Colon] = 0x33;
    // 单引号 ' 和 双引号 "
    m_keyMap[Qt::Key_Apostrophe] = 0x34; m_keyMap[Qt::Key_QuoteDbl] = 0x34;
    // 波浪号 ` 和 ~
    m_keyMap[Qt::Key_QuoteLeft] = 0x35; m_keyMap[Qt::Key_AsciiTilde] = 0x35;
    // 逗号 , 和 <
    m_keyMap[Qt::Key_Comma] = 0x36; m_keyMap[Qt::Key_Less] = 0x36;
    // 句号 . 和 >
    m_keyMap[Qt::Key_Period] = 0x37; m_keyMap[Qt::Key_Greater] = 0x37;
    // 斜杠 / 和 ?
    m_keyMap[Qt::Key_Slash] = 0x38; m_keyMap[Qt::Key_Question] = 0x38;
    // Caps Lock
    m_keyMap[Qt::Key_CapsLock] = 0x39;

    // F1 - F12
    m_keyMap[Qt::Key_F1]  = 0x3A;
    m_keyMap[Qt::Key_F2]  = 0x3B;
    m_keyMap[Qt::Key_F3]  = 0x3C;
    m_keyMap[Qt::Key_F4]  = 0x3D;
    m_keyMap[Qt::Key_F5]  = 0x3E;
    m_keyMap[Qt::Key_F6]  = 0x3F;
    m_keyMap[Qt::Key_F7]  = 0x40;
    m_keyMap[Qt::Key_F8]  = 0x41;
    m_keyMap[Qt::Key_F9]  = 0x42;
    m_keyMap[Qt::Key_F10] = 0x43;
    m_keyMap[Qt::Key_F11] = 0x44;
    m_keyMap[Qt::Key_F12] = 0x45;

    // 控制与导航区
    m_keyMap[Qt::Key_Print]      = 0x46;
    m_keyMap[Qt::Key_ScrollLock] = 0x47;
    m_keyMap[Qt::Key_Pause]      = 0x48; // Pause/Break
    m_keyMap[Qt::Key_Insert]     = 0x49;
    m_keyMap[Qt::Key_Home]       = 0x4A;
    m_keyMap[Qt::Key_PageUp]     = 0x4B;
    m_keyMap[Qt::Key_Delete]     = 0x4C;
    m_keyMap[Qt::Key_End]        = 0x4D;
    m_keyMap[Qt::Key_PageDown]   = 0x4E;

    // 方向键
    m_keyMap[Qt::Key_Right] = 0x4F;
    m_keyMap[Qt::Key_Left]  = 0x50;
    m_keyMap[Qt::Key_Down]  = 0x51;
    m_keyMap[Qt::Key_Up]    = 0x52;

    // 锁定键 (注意：这些通常需要同步状态，这里仅映射按键)
    m_keyMap[Qt::Key_NumLock]  = 0x53;
    // m_keyMap[Qt::Key_CapsLock] = 0x39; // 慎用，会导致 KVM 状态不同步
}

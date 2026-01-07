#include "pro_hidcontroller.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDateTime>
#include <QDebug>
#include <QThread>

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
    m_currentMode = MODE_NONE;                  //TODO：简化这一堆
    // 初始化长按定时器
    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true); // 只触发一次
    m_longPressTimer->setInterval(500);    // 设定触控模式，右键长按阈值：500ms
    connect(m_longPressTimer, &QTimer::timeout, this, &HidController::onLongPressTimeout);
    //丢弃事件
    m_elapsedTimer.start();
    m_lastSendTime = 0;
    m_accumRelDx = 0;
    m_accumRelDy = 0;
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
// 鼠标/键盘事件过滤器
// ==========================================

bool HidController::eventFilter(QObject *watched, QEvent *event) {
    if (m_currentMode == MODE_NONE || !m_driver) {
        return QObject::eventFilter(watched, event);
    }
    // 1. 自动处理尺寸变化
    if (event->type() == QEvent::Resize && watched->isWidgetType()) {
        m_widgetSize = static_cast<QWidget*>(watched)->size();
        updateScaleParams();
        return false; // Resize 应该继续传递给控件自身处理
    }
    // 2. 分流处理键盘与鼠标事件
    switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            processKey(static_cast<QKeyEvent*>(event), event->type() == QEvent::KeyPress);
            return true;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove:
        case QEvent::Wheel:
            if (watched->isWidgetType()) {
                processMouse(watched, event, event->type());
                return true;
            }
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

// ==========================================
// 鼠标执行逻辑
// ==========================================

void HidController::processMouse(QObject *watched, QEvent *evt, QEvent::Type type)
{
    if (!m_driver) return;

    QWidget *widget = qobject_cast<QWidget*>(watched);
    if (!widget) return;

    // === 1. 优先处理滚轮事件 (保持不变) ===
    if (type == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent*>(evt);
        int wheelSteps = we->angleDelta().y() / 120;
        int8_t hidWheel = qBound(-127, wheelSteps, 127);

        if (hidWheel != 0) {
            m_driver->sendMouseRel(0, 0, getHidButtonState(we->buttons()), hidWheel);
        }
        return;
    }

    // === 2. 准备通用数据 ===
    QMouseEvent *e = static_cast<QMouseEvent*>(evt);

    // 懒加载尺寸更新
    if (m_widgetSize != widget->size()) {
        m_widgetSize = widget->size();
        updateScaleParams();
    }

    // 获取按键状态
    uint8_t btnState = getHidButtonState(e->buttons());

    // === [新增] 限流核心逻辑 ===
    // 获取当前时间戳
    qint64 now = m_elapsedTimer.elapsed();
    // 检查是否达到发送间隔 (30ms 约等于 33Hz，足以保证流畅且不拥堵串口)
    bool isTimeUp = (now - m_lastSendTime) > 30;
    // Release 事件必须立即处理，不能等待
    bool isRelease = (type == QEvent::MouseButtonRelease);


    if (m_currentMode == MODE_ABSOLUTE)
    {
        // ============================================
        // === 绝对坐标模式 (带限流) ===
        // ============================================

        if (m_displayRect.isEmpty()) return;

        // 1. 计算坐标 (逻辑保持你的修正版不变)
        int realX = e->pos().x() - m_displayRect.x();
        int realY = e->pos().y() - m_displayRect.y();

        realX = qBound(0, realX, m_displayRect.width());
        realY = qBound(0, realY, m_displayRect.height());

        int hidX = 0;
        int hidY = 0;

        if (m_displayRect.width() > 0) {
            hidX = (int)((long long)realX * 4095 / m_displayRect.width());
        }
        if (m_displayRect.height() > 0) {
            hidY = (int)((long long)realY * 4095 / m_displayRect.height());
        }

        hidX = qBound(0, hidX, 4095);
        hidY = qBound(0, hidY, 4095);

        // 2. [新增] 限流发送逻辑
        // 如果是 Move 事件且时间未到，直接丢弃本次发送（因为绝对坐标只关心最新值）
        if (type == QEvent::MouseMove && !isTimeUp) {
            return;
        }

        // 发送指令 (时间到了，或者是点击/松开事件)
        m_driver->sendMouseAbs(hidX, hidY, btnState, 0);

        // 更新发送时间
        if (type == QEvent::MouseMove) m_lastSendTime = now;
    }
    else if (m_currentMode == MODE_RELATIVE)
    {
        // ============================================
        // === 相对坐标模式：触控板逻辑 + 累积限流 ===
        // ============================================

        if (type == QEvent::MouseButtonPress)
        {
            // 按下：初始化状态，清空累积量
            m_lastMousePos = e->globalPos();
            m_pressGlobalPos = e->globalPos();
            m_hasMoved = false;
            m_longPressTriggered = false;
            m_accumRelDx = 0; // [新增] 清空累积
            m_accumRelDy = 0; // [新增] 清空累积

            if (e->button() == Qt::RightButton) {
                m_driver->clickMouse(MOUSE_RIGHT);
                return;
            }
            m_longPressTimer->start();
        }
        else if (type == QEvent::MouseMove)
        {
            QPoint currentPos = e->globalPos();
            int dx = currentPos.x() - m_lastMousePos.x();
            int dy = currentPos.y() - m_lastMousePos.y();

            // 更新基准点 (这一步必须做，否则下一次计算dx会有问题)
            m_lastMousePos = currentPos;

            // 触控板逻辑：计算总位移
            int totalMove = (currentPos - m_pressGlobalPos).manhattanLength();

            if (totalMove > 5) {
                m_hasMoved = true;
                if (m_longPressTimer->isActive()) {
                    m_longPressTimer->stop();
                }
            }

            // 只有判定为滑动且未长按时，才累积数据
            if (m_hasMoved && !m_longPressTriggered) {
                // [新增] 累积位移，而不是立即发送
                m_accumRelDx += dx;
                m_accumRelDy += dy;

                // [新增] 限流检查
                if (isTimeUp) {
                    // 限制单次最大发送值
                    int sendDx = qBound(-127, m_accumRelDx, 127);
                    int sendDy = qBound(-127, m_accumRelDy, 127);

                    if (sendDx != 0 || sendDy != 0) {
                        // 相对滑动不带按键 (触控板模式)
                        m_driver->sendMouseRel(sendDx, sendDy, 0, 0);

                        // 从累积量中减去已发送的部分
                        m_accumRelDx -= sendDx;
                        m_accumRelDy -= sendDy;

                        // 更新时间戳
                        m_lastSendTime = now;
                    }
                }
            }
        }
        else if (type == QEvent::MouseButtonRelease)
        {
            m_longPressTimer->stop();
            if (e->button() == Qt::RightButton) return;

            // [新增] Release 时的缓冲区刷新逻辑
            // 如果用户在滑动过程中突然松手，缓冲区里可能还有残余的 dx/dy 没发出去
            // 我们需要把这部分发出去，保证位移的最终准确性
            if (m_hasMoved && (m_accumRelDx != 0 || m_accumRelDy != 0)) {
                int sendDx = qBound(-127, m_accumRelDx, 127);
                int sendDy = qBound(-127, m_accumRelDy, 127);
                if (sendDx != 0 || sendDy != 0) {
                    m_driver->sendMouseRel(sendDx, sendDy, 0, 0);
                }
                m_accumRelDx = 0;
                m_accumRelDy = 0;
            }

            // Tap 点击逻辑 (短按)
            if (!m_longPressTriggered && !m_hasMoved) {
                qDebug() << "Touch: Tap Detected (Left Click)";
                m_driver->clickMouse(MOUSE_LEFT);
            }
        }
    }
}
//void HidController::processMouse(QObject *watched, QEvent *evt, QEvent::Type type)
//{
//    if (!m_driver) return;

//    QWidget *widget = qobject_cast<QWidget*>(watched);
//    if (!widget) return;

//    if (type == QEvent::Wheel) {
//        QWheelEvent *we = static_cast<QWheelEvent*>(evt);
//        // 获取滚动方向：angleDelta().y() > 0 为前滚，负数为后滚
//        // CH9329 滚轮值：1 为向上，-1(0xFF) 为向下
//        int wheelSteps = we->angleDelta().y() / 120;
//        int8_t hidWheel = qBound(-127, wheelSteps, 127);

//        if (hidWheel != 0) {
//            // 注意：这里需要重新获取鼠标按键状态，QWheelEvent 也有 buttons() 方法
//            m_driver->sendMouseRel(0, 0, getHidButtonState(we->buttons()), hidWheel);
//        }
//        return;
//    }

//    // 将通用事件转为鼠标事件，供后续代码使用
//    QMouseEvent *e = static_cast<QMouseEvent*>(evt);

//    // 懒加载尺寸更新
//    if (m_widgetSize != widget->size()) {
//        m_widgetSize = widget->size();
//        updateScaleParams();
//    }

//    if (m_currentMode == MODE_ABSOLUTE)
//    {
//        // ============================================
//        // === 绝对坐标模式：解决黑边跳变与边界限制 ===
//        // ============================================

//        if (m_displayRect.isEmpty()) return;

//        // 1. 获取按键状态 (绝对模式下依然支持拖拽，所以保留 buttons 获取)
//        // 注意：getHidButtonState 是我们之前定义的辅助函数
//        uint8_t btnState = getHidButtonState(e->buttons());

//        // 2. 坐标映射与修正
//        // 先计算相对于图片显示区域的偏移
//        int realX = e->pos().x() - m_displayRect.x();
//        int realY = e->pos().y() - m_displayRect.y();

//        // [关键修正1]：在乘法运算前，先钳制 realX/realY
//        // 这一步确保了无论用户点在黑边多远的地方，都视为点在视频边缘
//        // 避免负数在后续计算中产生不可预料的溢出
//        realX = qBound(0, realX, m_displayRect.width());
//        realY = qBound(0, realY, m_displayRect.height());

//        // 3. 映射到 0~4095
//        // [关键修正2]：HID 12bit 坐标最大值通常建议为 4095 (0xFFF)，而不是 4096
//        int hidX = 0;
//        int hidY = 0;

//        if (m_displayRect.width() > 0) {
//            hidX = (int)((long long)realX * 4095 / m_displayRect.width());
//        }
//        if (m_displayRect.height() > 0) {
//            hidY = (int)((long long)realY * 4095 / m_displayRect.height());
//        }

//        // [关键修正3]：最终保险钳制
//        hidX = qBound(0, hidX, 4095);
//        hidY = qBound(0, hidY, 4095);

//        m_driver->sendMouseAbs(hidX, hidY, btnState, 0);
//    }
//    else if (m_currentMode == MODE_RELATIVE)
//    {
//        // ============================================
//        // === 相对坐标模式：触控板逻辑 (Tap=Click) ===
//        // ============================================

//        if (type == QEvent::MouseButtonPress)
//        {
//            // 按下：初始化状态
//            m_lastMousePos = e->globalPos();
//            m_pressGlobalPos = e->globalPos();
//            m_hasMoved = false;
//            m_longPressTriggered = false;

//            // 如果用户点击的是物理右键，立即响应，不启动长按逻辑（通常长按是为触控模拟右键准备的）
//            if (e->button() == Qt::RightButton) {
//                m_driver->clickMouse(MOUSE_RIGHT);
//                return;
//            }

//            // 启动长按定时器
//            m_longPressTimer->start();

//            // 注意：按下时不发送任何指令给电脑！
//            // 因为我们不知道用户是想点击、长按还是滑动。
//        }
//        else if (type == QEvent::MouseMove)
//        {
//            QPoint currentPos = e->globalPos();
//            int dx = currentPos.x() - m_lastMousePos.x();
//            int dy = currentPos.y() - m_lastMousePos.y();

//            // 计算从按下开始的总位移距离（曼哈顿距离或欧几里得距离均可）
//            int totalMove = (currentPos - m_pressGlobalPos).manhattanLength();

//            // 防抖阈值：只有移动超过 5 像素才视为“滑动”
//            if (totalMove > 5) {
//                m_hasMoved = true;

//                // 一旦开始滑动，就取消长按检测
//                if (m_longPressTimer->isActive()) {
//                    m_longPressTimer->stop();
//                }
//            }

//            // 只有被判定为“已移动”且没有触发长按时，才发送鼠标移动指令
//            if (m_hasMoved && !m_longPressTriggered) {
//                // 限制单次最大位移 (CH9329 限制)
//                int sendDx = qBound(-127, dx, 127);
//                int sendDy = qBound(-127, dy, 127);

//                if (sendDx != 0 || sendDy != 0) {
//                    // [关键] 相对模式滑动时，按钮状态恒为 0 (不按键)
//                    // 实现了“手指在屏幕上滑动 = 纯移动鼠标光标”
//                    m_driver->sendMouseRel(sendDx, sendDy, 0, 0);
//                    m_lastMousePos = currentPos;
//                }
//            }
//        }
//        else if (type == QEvent::MouseButtonRelease)
//        {
//            // 抬起：停止定时器
//            m_longPressTimer->stop();
//            // 如果是释放物理右键，不做额外处理
//            if (e->button() == Qt::RightButton) return;

//            // 逻辑判定：
//            // 1. 如果触发了长按 -> 已经在 Timer 槽函数里处理了(右键)，这里不做操作
//            // 2. 如果发生了滑动 -> 说明是移动操作结束，不做操作
//            // 3. 既没长按，也没滑动 -> 说明是“短按/点击” -> 发送左键点击
//            if (!m_longPressTriggered && !m_hasMoved) {
//                // 模拟一次完整的左键点击 (按下 + 抬起)
//                qDebug() << "Touch: Tap Detected (Left Click)";
//                m_driver->clickMouse(MOUSE_LEFT);
//            }
//        }
//    }
//}

// 长按判断逻辑
void HidController::onLongPressTimeout()
{
    // 如果定时器超时，说明用户按住且没有大幅移动 -> 触发右键
    if (m_currentMode == MODE_RELATIVE && m_driver) {
        qDebug() << "Touch: Long Press Triggered (Right Click)";
        m_driver->clickMouse(MOUSE_RIGHT);
        m_longPressTriggered = true; // 标记已触发
        // 可以在这里让手机震动一下反馈（如果Qt支持的话）
    }
}

// ==========================================
// 键盘执行逻辑
// ==========================================

void HidController::processKey(QKeyEvent *e, bool isPress) {
    // 1. 如果是重复按键事件(长按)，Qt会不断发 Press，硬件通常不需要重复发送
    if (e->isAutoRepeat()) return;

    // 2. 获取修饰键 (Ctrl/Shift/Alt)
    uint8_t mods = qtModifiersToHid(e->modifiers());

    // 3. 查找映射
    int key = e->key();

    // 特殊处理：如果仅仅按下了修饰键本身 (如只按了Ctrl)
    // Qt key 可能是 Key_Control，此时 mods 里面通常还没有这一位(Press时)或刚移除(Release时)
    // 但为了逻辑简单，我们统一：只要 m_keyMap 里有定义，就发送。
    // 修饰键本身在 HID 协议里通常不需要作为 KeyCode 发送，只需要在 Modifier Byte 里置位即可。
    // 但如果想支持 "按住 Ctrl 不放"，我们需要确保 sendKbPacket 被调用。
    // CH9329 只要 Modifier Byte 有值，且 KeyCode 为 0，也算有效包。

    if (m_keyMap.contains(key)) {
        uint8_t hidCode = m_keyMap[key];
        if (isPress) {
            m_driver->sendKbPacket(mods, hidCode);
        } else {
            // 松开时，发送全0。
            // 优化：如果是组合键，松开 A 但 Ctrl 没松，应该发送 Ctrl + 0。
            // 但 Qt 的 Release Event 里的 modifiers() 状态是“松开后的状态”吗？
            // 通常是的。所以我们再次获取 mods 发送即可。
            // 注意：如果是完全松开，mods 为 0，hidCode 为 0 (我们设为0)，相当于 releaseAll
            m_driver->sendKbPacket(mods, 0x00);
        }
    }
    else {
        // 键表中没找到键值，但如果修饰键有值 (比如只按了 Ctrl)，也应该发送更新
        // 比如按下 Ctrl (key=Key_Control)，我们不查表(表中无)，但 mods=Ctrl
        // 此时应该发送 [Ctrl, 0]。
        if (key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt || key == Qt::Key_Meta) {
            if (isPress) m_driver->sendKbPacket(mods, 0x00);
            else m_driver->sendKbPacket(mods, 0x00);
        }
    }
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

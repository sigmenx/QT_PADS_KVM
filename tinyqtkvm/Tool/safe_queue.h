#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

#include <queue>
#include <QMutex>
#include <QMutexLocker>
#include <cstdint>

// 定义通用指令结构体
struct HidCommand {
    enum Type {
        CMD_MOUSE_ABS,  // 绝对鼠标
        CMD_MOUSE_REL,  // 相对鼠标
        CMD_KEYBOARD    // 键盘
    };

    Type type;
    int param1; // x (abs/rel) or modifiers
    int param2; // y (abs/rel) or keycode
    int param3; // buttons
    int param4; // wheel
};

class HidPacketQueue {
public:
    static HidPacketQueue* instance() {
        static HidPacketQueue _instance;
        return &_instance;
    }

    void push(const HidCommand& cmd) {
        QMutexLocker locker(&m_mutex);
        m_queue.push(cmd);
    }

    bool pop(HidCommand& cmd) {
        QMutexLocker locker(&m_mutex);
        if (m_queue.empty()) return false;
        cmd = m_queue.front();
        m_queue.pop();
        return true;
    }

    // [新增] 清空队列（用于切换模式时防止积压）
    void clear() {
        QMutexLocker locker(&m_mutex);
        std::queue<HidCommand> empty;
        std::swap(m_queue, empty);
    }

private:
    std::queue<HidCommand> m_queue;
    QMutex m_mutex;
};

#endif // SAFE_QUEUE_H

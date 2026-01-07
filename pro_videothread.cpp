#include "pro_videothread.h"
#include <QDebug>

VideoController::VideoController(QObject *parent)
    : QThread(parent),
      m_abort(false),
      m_pause(true),       // 默认暂停
      m_reconfig(false),
      m_width(640), m_height(480), m_fmt(0), m_fps(30)
{
    // 在这里直接创建 camera 对象，确保指针永远不为 nullptr
    m_camera = new CameraDevice(this);
}

VideoController::~VideoController()
{
    quitThread();
}

// 主线程调用：开始/继续
void VideoController::startCapturing()
{
    QMutexLocker locker(&m_mutex);
    m_pause = false;
    m_cond.wakeOne(); // 唤醒可能在休眠的线程
}

// 主线程调用：暂停
void VideoController::stopCapturing()
{
    QMutexLocker locker(&m_mutex);
    m_pause = true;
}

// 主线程调用：修改分辨率
void VideoController::updateSettings(int width, int height, unsigned int fmt, int fps)
{
    QMutexLocker locker(&m_mutex);
    m_width = width;
    m_height = height;
    m_fmt = fmt;
    m_fps = fps;

    // 标记需要重配置，线程会在循环中处理
    m_reconfig = true;

    // 确保如果当前是暂停的，也能唤醒去处理配置
    m_pause = false;
    m_cond.wakeOne();
}

// 主线程调用：彻底退出
void VideoController::quitThread()
{
    {
        QMutexLocker locker(&m_mutex);
        m_abort = true;
        m_pause = false; // 确保不卡在 wait
    }
    m_cond.wakeOne(); // 唤醒线程让其退出 run 循环
    wait(); // 等待 run 函数返回
}

// ==========================================
// 核心工作循环 (子线程空间)
// ==========================================
void VideoController::run()
{
    qDebug() << "VideoController: Run loop started.";

    while (true) {
        // 1. 状态检查与锁保护
        {
            QMutexLocker locker(&m_mutex);

            // 检查是否要退出线程
            if (m_abort) break;

            // 检查是否暂停
            if (m_pause) {
                // 如果暂停，在此处休眠，释放 CPU，直到被 wakeOne 唤醒
                //m_camera->stopCapturing(); // 可选：暂停时真正停止硬件流
                m_cond.wait(&m_mutex);

                // 醒来后再次检查退出
                if (m_abort) break;
            }
        } // 释放锁，允许 captureFrame 进行耗时操作

        // 2. 检查是否需要应用新设置 (Reconfig)
        // 注意：这里需要再次加锁读取参数，但操作硬件不要加锁太久
        bool needReconfig = false;
        int w, h, fps;
        unsigned int fmt;

        {
            QMutexLocker locker(&m_mutex);
            if (m_reconfig) {
                needReconfig = true;
                m_reconfig = false; // 复位标志
                w = m_width; h = m_height; fmt = m_fmt; fps = m_fps;
            }
        }

        if (needReconfig && m_camera) {
            qDebug() << "VideoController: Reconfiguring camera...";
            m_camera->stopCapturing();
            // 注意：这里是在子线程调用 startCapturing，这是安全的
            if (!m_camera->startCapturing(w, h, fmt, fps)) {
                 //emit errorOccurred("Failed to start camera with new settings");
                 // 如果失败，可能需要暂停避免死循环报错
                 stopCapturing();
                 continue;
            }
        }

        // 3. 执行采集
        if (m_camera && m_camera->isCapturing()) {
            QImage image;
            // 此时 ioctl 会阻塞在这里，等待硬件信号，不占 CPU
            if (m_camera->captureFrame(image)) {
                // 4. 发送结果
                emit frameReady(image);
            }
        } else {
            // 只有在未采集时才需要小睡，防止没开流时死循环占满 CPU
            msleep(10);
        }

    }

    qDebug() << "VideoController: Run loop finished.";
}

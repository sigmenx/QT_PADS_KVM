#include "pro_videothread.h"
#include "../Tool/safe_queue.h"
#include <QDebug>

VideoController::VideoController(QObject *parent)
    : QThread(parent),
      m_abort(false), m_pause(true),
      m_dirtyCamera(false), m_dirtyNetwork(false), // 初始化参数更改标记
      m_cfgWidth(640), m_cfgHeight(480), m_cfgFmt(0), m_cfgFps(30),
      m_cfgNetOn(false), m_cfgPort(8080),
      m_encoder(nullptr), m_server(nullptr)
{
    m_camera = new CameraDevice(this);
}

VideoController::~VideoController()
{
    quitThread();
    // 线程结束后安全清理
    if (m_server) delete m_server;
    if (m_encoder) delete m_encoder;
}

// ================= 主线程接口 (只设置期望值 + 标记脏位) =================

void VideoController::startCapturing()
{
    QMutexLocker locker(&m_mutex);
    m_pause = false;
    m_cond.wakeOne();
}

void VideoController::stopCapturing()
{
    QMutexLocker locker(&m_mutex);
    m_pause = true;
}

void VideoController::updateSettings(int width, int height, unsigned int fmt, int fps)
{
    QMutexLocker locker(&m_mutex);
    // 1. 更新配置参数
    m_cfgWidth = width;
    m_cfgHeight = height;
    m_cfgFmt = fmt;
    m_cfgFps = fps;

    // 2. 仅标记摄像头脏了
    m_dirtyCamera = true;

    // 3. 唤醒线程处理
    m_pause = false;
    m_cond.wakeOne();
}

bool VideoController::startServer(int port)
{
    QMutexLocker locker(&m_mutex);
    // 1. 更新网络配置
    m_cfgNetOn = true;
    m_cfgPort = port;

    // 2. 仅标记网络脏了 (这样就不会重启摄像头)
    m_dirtyNetwork = true;

    m_cond.wakeOne();
    return true; // 实际成功与否在子线程处理，这里假设配置接受成功
}

void VideoController::stopServer()
{
    QMutexLocker locker(&m_mutex);
    m_cfgNetOn = false;
    m_dirtyNetwork = true;
}

void VideoController::quitThread()
{
    {
        QMutexLocker locker(&m_mutex);
        m_abort = true;
        m_pause = false;
    }
    m_cond.wakeOne();
    wait();
}

// ================= 子线程逻辑 =================

// 【核心优化】硬件状态同步机
// 这个函数负责将“期望参数”应用到“实际硬件”上
void VideoController::syncHardwareState()
{
    // 1. 读取并清除脏标记 (减少锁的持有时间)
    bool needCamReset = false;
    bool needNetReset = false;

    int targetW, targetH, targetFps, targetPort;
    unsigned int targetFmt;
    bool targetNetOn;

    {
        QMutexLocker locker(&m_mutex);
        if (m_dirtyCamera) {
            needCamReset = true;
            m_dirtyCamera = false;
        }
        if (m_dirtyNetwork) {
            needNetReset = true;
            m_dirtyNetwork = false;
        }
        // 拷贝参数
        targetW = m_cfgWidth; targetH = m_cfgHeight;
        targetFmt = m_cfgFmt; targetFps = m_cfgFps;
        targetNetOn = m_cfgNetOn; targetPort = m_cfgPort;
    }

    // 2. 处理摄像头变更 (优先级最高)
    if (needCamReset && m_camera) {
        //qDebug() << "Sync: Restarting Camera...";
        m_camera->stopCapturing();
        if (!m_camera->startCapturing(targetW, targetH, targetFmt, targetFps)) {
            //qDebug() << "Sync: Camera start failed!";
            QMutexLocker locker(&m_mutex);
            m_pause = true; // 失败则暂停
            return;
        }
        // 如果摄像头重启了，Encoder 必须重建 (因为分辨率变了)
        // 强制触发网络重置逻辑
        needNetReset = true;
    }

    // 3. 处理网络/编码器变更
    // 触发条件：网络开关切换 OR 摄像头刚刚重启过
    if (needNetReset) {
        //qDebug() << "Sync: Updating Network State...";

        // A. 清理旧资源
        if (m_encoder) { delete m_encoder; m_encoder = nullptr; }

        // Server 的处理比较特殊：如果只是分辨率变了(needNetReset由摄像头触发)，Server不用重启
        // 只有 targetNetOn 变了 或者 端口变了 才需要动 Server
        // 但为了简单，这里逻辑是：如果要开，确保有；如果要关，确保无。

        if (targetNetOn) {
            // 确保 Server 存在
            if (!m_server) {
                m_server = new WebServer(targetPort);
            }
            // 重建 Encoder (必须匹配当前摄像头参数)
            if (m_camera->getPixelFormat() == V4L2_PIX_FMT_YUYV) {
                int bitrate = targetW * targetH * 2;
                if (bitrate < 400000) bitrate = 400000;
                m_encoder = new VideoEncoder(targetW, targetH, bitrate);
                m_encoder->init();
                //qDebug() << "Sync: Encoder Created.";
            }
        } else {
            // 关闭 Server
            if (m_server) { delete m_server; m_server = nullptr; }
        }
    }
}

void VideoController::run()
{
    qDebug() << "VideoController: Run loop started.";

    while (true) {
        // --- 1. 线程控制与等待 ---
        {
            QMutexLocker locker(&m_mutex);
            if (m_abort) break;

            // 如果暂停 且 没有配置更改任务，则休眠
            // 注意：如果有 dirty 标记，即使是 pause 状态也要醒来处理配置
            if (m_pause && !m_dirtyCamera && !m_dirtyNetwork) {
                m_cond.wait(&m_mutex);
                if (m_abort) break;
            }
        }

        // --- 2. 状态同步 (核心重构) ---
        // 所有的 new/delete/restart 都在这里完成
        syncHardwareState();

        // --- 3. 网络接收 ---
        if (m_server) {
            m_server->handle_new_connections();
            auto msgs = m_server->process_client_messages();

            for (const auto& msg : msgs) {
                if (msg.empty()) continue;
                uint8_t type = msg[0];

                // === 解析逻辑移到这里 ===

                // 1. 鼠标包 [0x02, Buttons, X_L, X_H, Y_L, Y_H, Wheel]
                if (type == 0x02 && msg.size() >= 7) {
                    uint8_t buttons = msg[1];
                    uint16_t x_web = msg[2] | (msg[3] << 8);
                    uint16_t y_web = msg[4] | (msg[5] << 8);
                    int8_t wheel = (msg.size() > 6) ? (int8_t)msg[6] : 0;

                    // 计算坐标映射 (Web 32767 -> CH9329 4095)
                    int x_hid = (int)((long long)x_web * 4095 / 32767);
                    int y_hid = (int)((long long)y_web * 4095 / 32767);

                    // 构造命令并入队
                    HidCommand cmd;
                    cmd.type = HidCommand::CMD_MOUSE_ABS;
                    cmd.param1 = x_hid;
                    cmd.param2 = y_hid;
                    cmd.param3 = buttons;
                    cmd.param4 = wheel;
                    HidPacketQueue::instance()->push(cmd);
                }
                // 2. 键盘包 [0x01, Mods, Key]
                else if (type == 0x01 && msg.size() >= 3) {
                    HidCommand cmd;
                    cmd.type = HidCommand::CMD_KEYBOARD;
                    cmd.param1 = msg[1]; // Mods
                    cmd.param2 = msg[2]; // Key
                    HidPacketQueue::instance()->push(cmd);
                }
            }


        }

        // --- 4. 采集与分发 ---
        if (m_camera && m_camera->isCapturing()) {
            size_t len = 0;
            int index = -1;
            uint8_t* rawData = m_camera->dequeue(len, index);

            if (rawData) {
                // 分支1: 本地
                QImage img;
                m_camera->toQImage(rawData, len, img);
                emit frameReady(img);

                // 分支2: 网络 (直接使用成员变量，已经在 syncHardwareState 中保证了有效性)
                if (m_encoder && m_server && m_server->GetClientNumber() > 0) {
                    m_encoder->encode(rawData, [this](uint8_t* data, int size){
                        m_server->broadcast(data, size);
                    });
                }

                m_camera->enqueue(index);
            }
        } else {
            msleep(10);
        }
    }
    qDebug() << "VideoController: Run loop finished.";
}

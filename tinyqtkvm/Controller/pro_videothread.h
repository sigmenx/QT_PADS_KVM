#ifndef PRO_VIDEOTHREAD_H
#define PRO_VIDEOTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include "../Driver/drv_camera.h"
#include "../Driver/drv_webserver.h"
#include "../Tool/videoencoder.h"

class VideoController : public QThread
{
    Q_OBJECT
public:
    explicit VideoController(QObject *parent = nullptr);
    ~VideoController();

    // 【核心控制接口】
    // 开启采集 (如果已在运行则是继续)
    void startCapturing();

    // 暂停采集 (线程不退出，只是挂起)
    void stopCapturing();

    // 更新参数 (线程安全地更新配置，并在下一帧生效)
    void updateSettings(int width, int height, unsigned int fmt, int fps);

    // 彻底退出线程 (析构时调用)
    void quitThread();

    //server相关 开启视频转发
    bool startServer(int port);

    //关闭视频转发
    void stopServer();

    CameraDevice* m_camera;

protected:
    // 线程主循环
    void run() override;

signals:
    // 每一帧处理完发送信号
    void frameReady(QImage image);

    //向 ui线程 发送网络传入的 键鼠控制 信息
    //void remoteHidPacketReceived(std::vector<uint8_t> data);

    // 错误信号
    //void errorOccurred(QString msg);

private:

    // --- 线程同步与状态变量 ---
    QMutex m_mutex;
    QWaitCondition m_cond;

    bool m_abort;        // true: 彻底退出线程 loop
    bool m_pause;        // true: 暂停采集

    // --- 重配参数标志位 ---
    bool m_dirtyCamera;  // 只有分辨率/格式改变时置 true
    bool m_dirtyNetwork; // 只有开关网络服务时置 true

    // --- 期望参数 ---
    // 主线程只管写这些变量，子线程负责读取并应用
    int m_cfgWidth;
    int m_cfgHeight;
    unsigned int m_cfgFmt;
    int m_cfgFps;
    bool m_cfgNetOn; // 期望的网络开关状态
    int m_cfgPort;

    // --- 实际运行资源 ---
    VideoEncoder *m_encoder;
    WebServer *m_server;

    // 内部状态同步函数
    void syncHardwareState();

};

#endif // PRO_VIDEOTHREAD_H

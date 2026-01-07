#ifndef PRO_VIDEOTHREAD_H
#define PRO_VIDEOTHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include "drv_camera.h"

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

    CameraDevice* m_camera;

protected:
    // 线程主循环
    void run() override;

signals:
    // 每一帧处理完发送信号
    void frameReady(QImage image);
    // 错误信号
    //void errorOccurred(QString msg);

private:


    // --- 线程同步与状态变量 ---
    QMutex m_mutex;
    QWaitCondition m_cond;

    bool m_abort;        // true: 彻底退出线程 loop
    bool m_pause;        // true: 暂停采集
    bool m_reconfig;     // true: 需要重新配置摄像头参数

    // --- 缓存的摄像头参数 ---
    int m_width;
    int m_height;
    unsigned int m_fmt;
    int m_fps;
};

#endif // PRO_VIDEOTHREAD_H

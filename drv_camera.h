#ifndef DRV_CAMERA_H
#define DRV_CAMERA_H

#include <QString>
#include <QSize>
#include <QVector>
#include <QImage>
#include <QObject>

// Linux headers
#include <linux/videodev2.h>

struct VideoBuffer {
    void   *start;
    size_t  length;
};

class CameraDevice : public QObject
{
    Q_OBJECT
public:
    explicit CameraDevice(QObject *parent = nullptr);
    ~CameraDevice();

    // 设备控制
    bool openDevice(const QString &devicePath);
    void closeDevice();
    bool isOpened() const;

    // 参数查询
    QList<QPair<QString, unsigned int>> getSupportedFormats();
    QList<QSize> getResolutions(unsigned int pixelFormat);
    QList<int> getFramerates(unsigned int pixelFormat, int width, int height);

    // 采集控制
    bool startCapturing(int width, int height, unsigned int pixelFormat, int fps);
    void stopCapturing();
    bool isCapturing() const;

    // 获取一帧图像 (核心功能)
    bool captureFrame(QImage &image);

private:
    // 内部辅助函数
    void initMmap();
    void freeMmap();
    void yuyv_to_rgb(unsigned char *yuyv, unsigned char *rgb, int width, int height);

private:
    QString m_devicePath;
    int m_fd;
    bool m_isCapturing;

    // V4L2 Buffers
    VideoBuffer *m_buffers;
    unsigned int m_nBuffers;

    // 当前参数
    int m_width;
    int m_height;
    unsigned int m_pixelFormat;

    // 缓存池
    QVector<unsigned char> m_rgbBuffer;
};


#endif // DRV_CAMERA_H

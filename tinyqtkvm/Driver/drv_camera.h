#ifndef DRV_CAMERA_H
#define DRV_CAMERA_H

#include <QImage>
#include <QObject>
#include <QVector>

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

    // === [新增] 拆分 API (支持分流架构) ===

    // 1. 出队：获取一帧原始数据 (阻塞等待)
    //    返回: 原始数据指针 (失败返回 nullptr)
    //    out_len: 数据长度
    //    out_index: 缓冲区索引 (用于 enqueue)
    uint8_t* dequeue(size_t &out_len, int &out_index);

    // 2. 入队：归还缓冲区给内核
    void enqueue(int index);

    // 3. 转换：将原始数据转为 QImage (用于 UI 显示)
    //    支持 YUYV (软转码) 和 MJPEG (软解码)
    void toQImage(const uint8_t* rawData, size_t len, QImage &outImage);

    // [兼容旧接口] 内部自动调用上述三个函数
    bool captureFrame(QImage &image);

    // 获取当前像素格式 (供 VideoThread 判断是否允许转发)
    unsigned int getPixelFormat() const { return m_pixelFormat; }

private:
    // 内部辅助函数
    void initMmap();
    void freeMmap();
    // 注意：这里改为 const 输入
    void yuyv_to_rgb(const unsigned char *yuyv, unsigned char *rgb, int width, int height);

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

    // 缓存池 (RGB数据容器)
    QVector<unsigned char> m_rgbBuffer;
};

#endif // DRV_CAMERA_H

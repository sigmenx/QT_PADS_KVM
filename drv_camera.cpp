#include "drv_camera.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <QDebug>
#include <cstring> // for memset

CameraDevice::CameraDevice(QObject *parent) : QObject(parent),
    m_fd(-1), m_isCapturing(false), m_buffers(nullptr), m_nBuffers(0)
{
}

CameraDevice::~CameraDevice()
{
    //stopCapturing();
    closeDevice();
}

bool CameraDevice::openDevice(const QString &devicePath)
{
    closeDevice(); // 确保之前的关闭
    m_devicePath = devicePath;
    m_fd = ::open(devicePath.toLocal8Bit().data(), O_RDWR); // 阻塞模式
    if (m_fd < 0) {
        perror("Open device failed");
        return false;
    }
    return true;
}

void CameraDevice::closeDevice()
{
    stopCapturing();
    usleep(200000);//延时200ms
    if (m_fd != -1) {
        ::close(m_fd);
        usleep(200000);//延时200ms
        m_fd = -1;
    }

}

bool CameraDevice::isOpened() const { return m_fd != -1; }
bool CameraDevice::isCapturing() const { return m_isCapturing; }

// ================= V4L2 查询逻辑 (封装简化) =================

QList<QPair<QString, unsigned int>> CameraDevice::getSupportedFormats()
{
    QList<QPair<QString, unsigned int>> formats;
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    for (int i = 0; ; ++i) {
        fmtdesc.index = i;
        if (ioctl(m_fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) break;
        formats.append({QString((char*)fmtdesc.description), fmtdesc.pixelformat});
    }
    return formats;
}

QList<QSize> CameraDevice::getResolutions(unsigned int pixelFormat)
{
    QList<QSize> sizes;
    struct v4l2_frmsizeenum frmsize;
    memset(&frmsize, 0, sizeof(frmsize));
    frmsize.pixel_format = pixelFormat;

    for (int i = 0; ; ++i) {
        frmsize.index = i;
        if (ioctl(m_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0) break;
        if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            sizes.append(QSize(frmsize.discrete.width, frmsize.discrete.height));
        }
    }
    return sizes;
}

QList<int> CameraDevice::getFramerates(unsigned int pixelFormat, int width, int height)
{
    QList<int> fpsList;
    struct v4l2_frmivalenum frmival;
    memset(&frmival, 0, sizeof(frmival));
    frmival.pixel_format = pixelFormat;
    frmival.width = width;
    frmival.height = height;

    for (int i = 0; ; ++i) {
        frmival.index = i;
        if (ioctl(m_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) < 0) break;
        if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            fpsList.append(frmival.discrete.denominator / frmival.discrete.numerator);
        }
    }
    return fpsList;
}

// ================= 采集控制 =================

bool CameraDevice::startCapturing(int width, int height, unsigned int pixelFormat, int fps)
{
    if (m_fd < 0) return false;
    stopCapturing(); // 先停止

    // 1. 设置格式
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixelFormat;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) return false;

    // 更新实际参数
    m_width = fmt.fmt.pix.width;
    m_height = fmt.fmt.pix.height;
    m_pixelFormat = pixelFormat;

    // 2. 设置帧率
    struct v4l2_streamparm streamparm;
    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = fps;
    ioctl(m_fd, VIDIOC_S_PARM, &streamparm);

    // 3. 申请缓存
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) return false;

    // 4. Mmap
    m_buffers = (VideoBuffer*)calloc(req.count, sizeof(*m_buffers));
    m_nBuffers = req.count;
    initMmap();

    // 5. 开启流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0) return false;

    // 6. 预分配 RGB 缓冲区，避免循环中重复 new
    if (m_pixelFormat == V4L2_PIX_FMT_YUYV) {
        m_rgbBuffer.resize(m_width * m_height * 3);
    }

    m_isCapturing = true;
    return true;
}

void CameraDevice::stopCapturing()
{
    // 如果没有在采集，直接返回 (但要注意如果是析构调用，无论如何都要清理)
    if (!m_isCapturing) return;

    // 1. 停止视频流 (Stream Off)
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Streamoff failed");
    }

    // 2. 解除用户空间映射 (munmap)
    freeMmap();
    if (m_buffers) {
        free(m_buffers);
        m_buffers = nullptr;
    }

    // =========================================================
    // 【关键修正】 3. 释放内核缓冲区 (REQBUFS count=0)
    // 必须告诉内核释放内存，否则下次 S_FMT 会报 EBUSY
    // =========================================================
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0; // 0 表示释放
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(m_fd, VIDIOC_REQBUFS, &req);

    m_nBuffers = 0;
    m_isCapturing = false;
}



void CameraDevice::initMmap()
{
    for (unsigned int i = 0; i < m_nBuffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(m_fd, VIDIOC_QUERYBUF, &buf);

        m_buffers[i].length = buf.length;
        m_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
        ioctl(m_fd, VIDIOC_QBUF, &buf); // 入队
    }
}

void CameraDevice::freeMmap()
{
    for (unsigned int i = 0; i < m_nBuffers; ++i) {
        munmap(m_buffers[i].start, m_buffers[i].length);
    }
}

// ================= 获取帧逻辑 =================

bool CameraDevice::captureFrame(QImage &image)
{
    if (!m_isCapturing || !m_buffers || m_fd < 0) return false;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // 1. 出队 (DQBUF)
    if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) return false;

    // 2. 数据处理
    bool success = false;
    unsigned char *rawData = (unsigned char*)m_buffers[buf.index].start;

    if (m_pixelFormat == V4L2_PIX_FMT_YUYV) {
        // 使用成员变量 buffer，无需 new
        yuyv_to_rgb(rawData, m_rgbBuffer.data(), m_width, m_height);
        // Deep copy to QImage (QImage wraps the data, we make a copy via copy())
        // 注意：QImage 构造函数默认不拷贝数据，但我们需要让 image 独立于 m_rgbBuffer，所以建议 copy
        // 或者直接生成 Image。为了安全，这里生成一个新的 image。
        // 260104：去掉 .copy()。QImage 只是一个 Header，指向 m_rgbBuffer
        image = QImage(m_rgbBuffer.data(), m_width, m_height, QImage::Format_RGB888);//.copy();
        success = true;
    } else if (m_pixelFormat == V4L2_PIX_FMT_MJPEG) {
        image.loadFromData(rawData, buf.bytesused);
        success = true;
    }

    // 3. 入队 (QBUF)
    ioctl(m_fd, VIDIOC_QBUF, &buf);

    return success;
}

void CameraDevice::yuyv_to_rgb(unsigned char *yuyv, unsigned char *rgb, int width, int height)
{
    // 你的原始算法，未改动
    int y0, u, y1, v;
    int r0, g0, b0, r1, g1, b1;
    int i = 0, j = 0;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col += 2) {
            y0 = yuyv[i++]; u  = yuyv[i++];
            y1 = yuyv[i++]; v  = yuyv[i++];
            r0 = y0 + 1.402 * (v - 128);
            g0 = y0 - 0.34414 * (u - 128) - 0.71414 * (v - 128);
            b0 = y0 + 1.772 * (u - 128);
            r1 = y1 + 1.402 * (v - 128);
            g1 = y1 - 0.34414 * (u - 128) - 0.71414 * (v - 128);
            b1 = y1 + 1.772 * (u - 128);
            auto clamp = [](int x) { return (x < 0) ? 0 : ((x > 255) ? 255 : x); };
            rgb[j++] = clamp(r0); rgb[j++] = clamp(g0); rgb[j++] = clamp(b0);
            rgb[j++] = clamp(r1); rgb[j++] = clamp(g1); rgb[j++] = clamp(b1);
        }
    }
}

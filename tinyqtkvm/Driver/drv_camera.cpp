#include "drv_camera.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <cstring>
#include <cstdlib> // for calloc/free

CameraDevice::CameraDevice(QObject *parent) : QObject(parent),
    m_fd(-1), m_isCapturing(false), m_buffers(nullptr), m_nBuffers(0)
{
}

CameraDevice::~CameraDevice()
{
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
    usleep(20000); // 延时20ms即可，200ms太长
    if (m_fd != -1) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool CameraDevice::isOpened() const { return m_fd != -1; }
bool CameraDevice::isCapturing() const { return m_isCapturing; }

// ================= V4L2 查询逻辑 =================

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

    // 6. 预分配 RGB 缓冲区 (仅 YUYV 需要)
    if (m_pixelFormat == V4L2_PIX_FMT_YUYV) {
        m_rgbBuffer.resize(m_width * m_height * 3);
    }

    m_isCapturing = true;
    return true;
}

void CameraDevice::stopCapturing()
{
    if (!m_isCapturing) return;

    // 1. 停止视频流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(m_fd, VIDIOC_STREAMOFF, &type);

    // 2. 解除映射
    freeMmap();
    if (m_buffers) {
        free(m_buffers);
        m_buffers = nullptr;
    }

    // 3. 释放内核缓冲区
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0;
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

// =================  视频数据流 拆分 API 实现 =================

// 1. 出队：获取原始数据
uint8_t* CameraDevice::dequeue(size_t &out_len, int &out_index)
{
    if (!m_isCapturing || !m_buffers || m_fd < 0) return nullptr;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // 阻塞等待硬件完成一帧采集
    if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
        // perror("DQBUF failed"); // 调试时可打开
        return nullptr;
    }

    out_index = buf.index;
    out_len = buf.bytesused;

    // 返回 mmap 地址，直接指向内核缓冲，无需拷贝
    return static_cast<uint8_t*>(m_buffers[buf.index].start);
}

// 2. 入队：归还缓冲区
void CameraDevice::enqueue(int index)
{
    if (m_fd < 0 || index < 0) return;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;

    // 告诉内核该缓冲已处理完毕，可以再次填充数据
    ioctl(m_fd, VIDIOC_QBUF, &buf);
}

// 3. 转换：Raw -> QImage
void CameraDevice::toQImage(const uint8_t* rawData, size_t len, QImage &outImage)
{
    if (!rawData) return;

    if (m_pixelFormat == V4L2_PIX_FMT_YUYV) {
        // 软转码：YUYV -> RGB (写入 m_rgbBuffer)
        yuyv_to_rgb(rawData, m_rgbBuffer.data(), m_width, m_height);

        // 深拷贝：生成独立的 QImage，防止 m_rgbBuffer 在下一帧被覆盖时影响 UI 显示
        // 注意：QImage(data, ...) 构造函数默认浅拷贝，必须加 .copy()
        outImage = QImage(m_rgbBuffer.data(), m_width, m_height, QImage::Format_RGB888).copy();

    } else if (m_pixelFormat == V4L2_PIX_FMT_MJPEG) {
        // MJPEG 直接解压
        outImage.loadFromData(rawData, len);
        // loadFromData 内部会自动创建深拷贝的内存
    }
}

// [兼容接口] 旧逻辑的 wrapper
bool CameraDevice::captureFrame(QImage &image)
{
    size_t len = 0;
    int index = -1;

    // 1. 获取原始数据
    uint8_t* raw = dequeue(len, index);
    if (!raw) return false;

    // 2. 转换为图像
    toQImage(raw, len, image);

    // 3. 归还缓冲区
    enqueue(index);

    return true;
}

// 内部算法 (输入改为 const)
void CameraDevice::yuyv_to_rgb(const unsigned char *yuyv, unsigned char *rgb, int width, int height)
{
    int y0, u, y1, v;
    int r0, g0, b0, r1, g1, b1;
    int i = 0, j = 0;

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col += 2) {
            y0 = yuyv[i++]; u  = yuyv[i++];
            y1 = yuyv[i++]; v  = yuyv[i++];

            // 你的原始优化算法
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

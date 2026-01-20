#ifndef VIDEOENCODER_H
#define VIDEOENCODER_H

#include <vector>
#include <iostream>
#include <functional>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// 定义回调函数类型：void(数据指针, 数据大小)
using EncodeCallback = std::function<void(uint8_t*, int)>;

class VideoEncoder {
public:
    VideoEncoder(int width, int height, int bitrate = 400000);
    ~VideoEncoder();

    // 初始化 FFmpeg 编码器资源
    bool init();

    // 核心函数：输入 YUYV -> 输出 H.264 (通过 callback)
    void encode(const void* yuyv_data, EncodeCallback callback);

private:
    int width_;
    int height_;
    int bitrate_;
    int frame_count_ = 0;

    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_yuv420_ = nullptr; // 用于存放转换后的 YUV420P 数据
    AVPacket* pkt_ = nullptr;         // 用于存放编码后的压缩数据
    struct SwsContext* sws_ctx_ = nullptr; // 图像格式转换上下文
};

#endif // VIDEOENCODER_H

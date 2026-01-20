#include "videoencoder.h"

VideoEncoder::VideoEncoder(int width, int height, int bitrate)
    : width_(width), height_(height), bitrate_(bitrate) {}

VideoEncoder::~VideoEncoder() {
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
    if (frame_yuv420_) av_frame_free(&frame_yuv420_);
    if (pkt_) av_packet_free(&pkt_);
    if (sws_ctx_) sws_freeContext(sws_ctx_);
}

bool VideoEncoder::init() {
    // 1. 查找 H.264 编码器 (libx264)
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "[Encoder] H.264 Encoder not found!" << std::endl;
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) return false;

    // 2. 配置编码参数
    codec_ctx_->bit_rate = bitrate_;       
    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    codec_ctx_->time_base = {1, 30};       
    codec_ctx_->framerate = {30, 1};       
    codec_ctx_->gop_size = 30;             
    codec_ctx_->max_b_frames = 0;          // 零延迟关键：禁用 B 帧
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;

    // 3. 设置 x264 私有参数 (极低延迟模式)
    av_opt_set(codec_ctx_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(codec_ctx_, codec, NULL) < 0) {
        std::cerr << "[Encoder] Could not open codec" << std::endl;
        return false;
    }

    // 4. 分配 YUV420P 帧内存
    frame_yuv420_ = av_frame_alloc();
    frame_yuv420_->format = codec_ctx_->pix_fmt;
    frame_yuv420_->width = codec_ctx_->width;
    frame_yuv420_->height = codec_ctx_->height;
    
    // 为 frame 分配 buffer
    if (av_frame_get_buffer(frame_yuv420_, 32) < 0) {
        std::cerr << "[Encoder] Could not allocate frame data" << std::endl;
        return false;
    }

    pkt_ = av_packet_alloc();

    // 5. 初始化图像转换上下文 (YUYV -> YUV420P)
    sws_ctx_ = sws_getContext(width_, height_, AV_PIX_FMT_YUYV422,
                              width_, height_, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, NULL, NULL, NULL);
    
    if (!sws_ctx_) {
        std::cerr << "[Encoder] Could not initialize SwsContext" << std::endl;
        return false;
    }

    return true;
}

void VideoEncoder::encode(const void* yuyv_data, EncodeCallback callback) {
    if (!codec_ctx_ || !frame_yuv420_ || !sws_ctx_) return;

    // 1. 格式转换: YUYV (Packed) -> YUV420P (Planar)
    // YUYV 步长 = width * 2
    const uint8_t* srcSlice[] = { (const uint8_t*)yuyv_data };
    int srcStride[] = { width_ * 2 };
    
    // 执行转换
    sws_scale(sws_ctx_, srcSlice, srcStride, 0, height_,
              frame_yuv420_->data, frame_yuv420_->linesize);

    // 设置 PTS (Presentation Time Stamp)，防止 FFmpeg 警告
    frame_yuv420_->pts = frame_count_++;

    // 2. 发送帧给编码器
    int ret = avcodec_send_frame(codec_ctx_, frame_yuv420_);
    if (ret < 0) {
        std::cerr << "[Encoder] Error sending frame to codec" << std::endl;
        return;
    }

    // 3. 接收编码后的数据包
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "[Encoder] Error during encoding" << std::endl;
            break;
        }

        // 调用回调发送数据
        if (callback) {
            callback(pkt_->data, pkt_->size);
        }

        av_packet_unref(pkt_);
    }
}

#pragma once

#include "queue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 视频编码器配置参数
struct VideoEncoderParams {
    int width = 0;
    int height = 0;
    int fps = 25;
    int bitrate = 1000000;  // 1Mbps
    AVCodecID codec_id = AV_CODEC_ID_MPEG4;  // 默认使用MPEG4
    AVPixelFormat pixel_format = AV_PIX_FMT_YUV420P;
    int gop_size = 12;      // 关键帧间隔
    int max_b_frames = 2;   // B帧数量
};

// 视频编码线程函数
void video_encode_thread_func(VideoFrameQueue* video_frame_queue, 
                              EncodedVideoPacketQueue* encoded_video_queue,
                              const VideoEncoderParams& params);

// 便利函数：使用默认参数编码
void video_encode_thread_func_simple(VideoFrameQueue* video_frame_queue, 
                                     EncodedVideoPacketQueue* encoded_video_queue,
                                     int width, int height, int fps = 25);
#pragma once
#include "queue.h"

extern "C" {
#include <libavformat/avformat.h>
}

// 解封装器配置参数
struct DemuxerParams {
    const char* input_filename = nullptr;
    int max_frames = 0;  // 0表示处理所有帧，>0表示限制处理的帧数
    bool enable_audio = true;// 是否启用音频解封装
    // 是否启用视频解封装
    bool enable_video = true;
};

// 解封装线程函数
void demux_thread_func(const char* input_filename,
                       VideoPacketQueue* video_packet_queue,
                       AudioPacketQueue* audio_packet_queue);

// 带参数的解封装线程函数
void demux_thread_func_with_params(const DemuxerParams& params,
                                  VideoPacketQueue* video_packet_queue,
                                  AudioPacketQueue* audio_packet_queue);

// 获取流信息的辅助函数
struct StreamInfo {
    int video_stream_index = -1;// 视频流索引
    int audio_stream_index = -1;
    int video_width = 0;
    int video_height = 0;
    int video_fps = 25;
    AVPixelFormat video_pixel_format = AV_PIX_FMT_YUV420P;// 默认YUV420P
    int audio_sample_rate = 48000;// 默认48kHz
    int audio_channels = 2;
    AVSampleFormat audio_sample_format = AV_SAMPLE_FMT_FLTP;//AVSampleFormat 枚举定义了音频样本格式，表示音频样本格式为浮点型（float），且采用平面存储方式
    //AVCodecParameters 是一个结构体，用于描述编码流的属性。它包含了视频和音频相关的编码信息，
    //例如编码类型、比特率、视频分辨率、音频采样率等，并需通过 avcodec_parameters_alloc() 分配内存以及通过 avcodec_parameters_free() 释放内存。
    AVCodecParameters* video_codec_params = nullptr;// 视频编解码器参数
    // 注意：音频编解码器参数可能为nullptr，表示没有音频
    AVCodecParameters* audio_codec_params = nullptr;
};

bool get_stream_info(const char* input_filename, StreamInfo& info);

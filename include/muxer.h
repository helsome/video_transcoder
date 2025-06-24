#pragma once

#include "queue.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// 封装器配置参数
struct MuxerParams {
    const char* output_filename = nullptr;
    const char* format_name = "avi";  // 默认输出AVI格式
    
    // 视频参数
    int video_width = 0;
    int video_height = 0;
    int video_fps = 25;
    AVCodecID video_codec_id = AV_CODEC_ID_MPEG4;
    
    // 音频参数
    int audio_sample_rate = 48000;
    int audio_channels = 2;
    AVCodecID audio_codec_id = AV_CODEC_ID_AC3;  // 默认使用AC3
};

// 视频封装器配置参数
struct VideoMuxerParams {
    const char* output_filename = nullptr;
    const char* format_name = "avi";
    int video_width = 0;
    int video_height = 0;
    int video_fps = 25;
    AVCodecID video_codec_id = AV_CODEC_ID_MPEG4;
    int bitrate = 800000;
};

// 音频封装器配置参数
struct AudioMuxerParams {
    const char* output_filename = nullptr;
    const char* format_name = "ac3";  // 默认输出AC3格式
    int audio_sample_rate = 48000;
    int audio_channels = 2;
    AVCodecID audio_codec_id = AV_CODEC_ID_AC3;  // 默认使用AC3
    int bitrate = 128000;
};

// 主要Mux线程函数（音视频合并）
void mux_thread_func(EncodedVideoPacketQueue* video_packet_queue,
                     EncodedAudioPacketQueue* audio_packet_queue,
                     const MuxerParams& params);

// 视频专用Mux线程函数
void video_mux_thread_func(EncodedVideoPacketQueue* video_packet_queue,
                          const VideoMuxerParams& params);

// 音频专用Mux线程函数
void audio_mux_thread_func(EncodedAudioPacketQueue* audio_packet_queue,
                          const AudioMuxerParams& params);

// 便利函数：使用默认参数封装
void mux_thread_func_simple(EncodedVideoPacketQueue* video_packet_queue,
                           EncodedAudioPacketQueue* audio_packet_queue,
                           const char* output_filename,
                           int video_width, int video_height, int video_fps,
                           int audio_sample_rate, int audio_channels);
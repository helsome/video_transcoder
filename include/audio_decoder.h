#pragma once
#include "queue.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

// 原有的解码到文件函数（保持兼容性）
void audio_decode_thread_func(AudioPacketQueue* audio_packet_queue,
                             AVCodecParameters* codec_params,
                             const char* output_filename);

// 新的解码到Frame队列函数（用于完整转码流程）
void audio_decode_to_frames_thread_func(AudioPacketQueue* audio_packet_queue,
                                        AudioFrameQueue* audio_frame_queue,
                                        AVCodecParameters* codec_params);
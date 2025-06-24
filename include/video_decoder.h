#pragma once

#include "queue.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

// 原有的解码到文件函数（保持兼容性）
void video_decode_thread_func(VideoPacketQueue* video_packet_queue, 
                              AVCodecParameters* codec_params, 
                              const char* output_filename);

// 新的解码到Frame队列函数（用于完整转码流程）
void video_decode_to_frames_thread_func(VideoPacketQueue* video_packet_queue,
                                        VideoFrameQueue* video_frame_queue,
                                        AVCodecParameters* codec_params);

#include "queue.h"
#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 辅助函数：创建并分配AVPacket
AVPacket* create_packet() {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "错误: 无法分配AVPacket" << std::endl;
        return nullptr;
    }
    return packet;
}

// 辅助函数：创建并分配AVFrame
AVFrame* create_frame() {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "错误: 无法分配AVFrame" << std::endl;
        return nullptr;
    }
    return frame;
}

// 辅助函数：安全释放AVPacket
void safe_free_packet(AVPacket** packet) {
    if (packet && *packet) {
        av_packet_free(packet);
        *packet = nullptr;
    }
}

// 辅助函数：安全释放AVFrame
void safe_free_frame(AVFrame** frame) {
    if (frame && *frame) {
        av_frame_free(frame);
        *frame = nullptr;
    }
}

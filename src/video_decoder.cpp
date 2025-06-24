#include "video_decoder.h"
#include <iostream>
#include <fstream>

void save_yuv_frame(AVFrame* frame, const char* filename) {
    std::ofstream file(filename, std::ios::app | std::ios::binary);
    if (!file) {
        std::cerr << "无法打开YUV输出文件。" << std::endl;
        return;
    }

    // --- 写入 Y (Luma) 分量 ---
    if (frame->linesize[0] == frame->width) {
        file.write(reinterpret_cast<const char*>(frame->data[0]), frame->width * frame->height);
    } else { 
        for (int i = 0; i < frame->height; i++) {
            file.write(reinterpret_cast<const char*>(frame->data[0] + i * frame->linesize[0]), frame->width);
        }
    }

    // --- 写入 U (Cb) 分量 ---
    if (frame->linesize[1] == frame->width / 2) {
        file.write(reinterpret_cast<const char*>(frame->data[1]), (frame->width / 2) * (frame->height / 2));
    } else {
        for (int i = 0; i < frame->height / 2; i++) {
            file.write(reinterpret_cast<const char*>(frame->data[1] + i * frame->linesize[1]), frame->width / 2);
        }
    }

    // --- 写入 V (Cr) 分量 ---
    if (frame->linesize[2] == frame->width / 2) {
        file.write(reinterpret_cast<const char*>(frame->data[2]), (frame->width / 2) * (frame->height / 2));
    } else {
        for (int i = 0; i < frame->height / 2; i++) {
            file.write(reinterpret_cast<const char*>(frame->data[2] + i * frame->linesize[2]), frame->width / 2);
        }
    }

    file.close();
}

void video_decode_thread_func(VideoPacketQueue* video_packet_queue, 
                              AVCodecParameters* codec_params,
                              const char* output_filename) {
    std::cout << "视频解码线程已启动。" << std::endl;
    
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "未找到视频解码器，ID: " << codec_params->codec_id << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "无法分配视频解码器上下文。" << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    if (avcodec_parameters_to_context(codec_context, codec_params) < 0) {
        std::cerr << "无法将解码参数复制到视频解码器上下文。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "无法打开视频解码器。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
         std::cerr << "无法分配视频帧。" << std::endl;
         avcodec_free_context(&codec_context);
         avcodec_parameters_free(&codec_params);
         return;
    }

    int frame_count = 0;
    const int max_frames_to_save = 20;

    std::ofstream ofs(output_filename, std::ios::trunc);
    ofs.close();

    while (true) {
        AVPacket* packet = nullptr;
        if (!video_packet_queue->pop(packet) || packet == nullptr) {
            break;
        }

        int ret = avcodec_send_packet(codec_context, packet);
        av_packet_free(&packet);

        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_context, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }

            if (frame_count < max_frames_to_save) {
                 if (frame_count == 0) {
                     std::cout << "解码出的第一帧分辨率: " << frame->width << "x" << frame->height << std::endl;
                     std::cout << "解码出的第一帧像素格式ID: " << frame->format << std::endl;
                 }
                 save_yuv_frame(frame, output_filename);

                 frame_count++;
            }
        }
        if (frame_count >= max_frames_to_save) {
            AVPacket* packet = nullptr;
            while (video_packet_queue->pop(packet) && packet != nullptr) {
                av_packet_free(&packet);
            }
            break;
        }
    }
    
    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
    avcodec_parameters_free(&codec_params);
    
    std::cout << "视频解码线程已结束。共保存 " << frame_count << " 帧。" << std::endl;
}

// 新的解码到Frame队列函数（用于完整转码流程）
void video_decode_to_frames_thread_func(VideoPacketQueue* video_packet_queue,
                                        VideoFrameQueue* video_frame_queue,
                                        AVCodecParameters* codec_params) {
    std::cout << "视频解码线程（输出到Frame队列）已启动。" << std::endl;
    
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "未找到视频解码器，ID: " << codec_params->codec_id << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "无法分配视频解码器上下文。" << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    if (avcodec_parameters_to_context(codec_context, codec_params) < 0) {
        std::cerr << "无法将解码参数复制到视频解码器上下文。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "无法打开视频解码器。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
         std::cerr << "无法分配视频帧。" << std::endl;
         avcodec_free_context(&codec_context);
         avcodec_parameters_free(&codec_params);
         return;
    }

    int frame_count = 0;

    while (true) {
        AVPacket* packet = nullptr;
        if (!video_packet_queue->pop(packet) || packet == nullptr) {
            break;
        }

        int ret = avcodec_send_packet(codec_context, packet);
        av_packet_free(&packet);

        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_context, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }
            AVFrame* output_frame = av_frame_alloc();
            if (av_frame_ref(output_frame, frame) < 0) {
                std::cerr << "无法复制视频帧。" << std::endl;
                av_frame_free(&output_frame);
                continue;
            }
            
            video_frame_queue->push(output_frame);
            frame_count++;
        }
    }
    
    // 标记帧队列结束
    video_frame_queue->finish();

    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
    avcodec_parameters_free(&codec_params);
    
    std::cout << "视频解码线程（输出到Frame队列）已结束。共解码 " << frame_count << " 帧。" << std::endl;
}

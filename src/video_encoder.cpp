#include "video_encoder.h"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

void video_encode_thread_func(VideoFrameQueue* video_frame_queue, 
                              EncodedVideoPacketQueue* encoded_video_queue,
                              const VideoEncoderParams& params) {
    std::cout << "视频编码线程已启动，使用编码器: " << avcodec_get_name(params.codec_id) << std::endl;
    
    // 查找编码器
    const AVCodec* codec = avcodec_find_encoder(params.codec_id);
    if (!codec) {
        std::cerr << "未找到视频编码器，ID: " << params.codec_id << std::endl;
        return;
    }

    // 分配编码器上下文
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "无法分配视频编码器上下文。" << std::endl;
        return;
    }

    // 设置编码参数
    codec_context->bit_rate = params.bitrate;
    codec_context->width = params.width;
    codec_context->height = params.height;
    codec_context->time_base = {1, params.fps};
    codec_context->framerate = {params.fps, 1};
    codec_context->gop_size = params.gop_size;
    codec_context->max_b_frames = params.max_b_frames;
    codec_context->pix_fmt = params.pixel_format;

    // 针对不同编码器的特殊设置
    if (params.codec_id == AV_CODEC_ID_H264) {
        av_opt_set(codec_context->priv_data, "preset", "fast", 0);
        av_opt_set(codec_context->priv_data, "tune", "zerolatency", 0);
    } else if (params.codec_id == AV_CODEC_ID_MPEG4) {
        // MPEG4编码器设置
        codec_context->qmin = 2;
        codec_context->qmax = 31;
        codec_context->qcompress = 0.6f;
        // 设置MPEG4特有的选项
        av_opt_set_int(codec_context, "mpeg_quant", 1, 0);
    }

    // 打开编码器
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "无法打开视频编码器。" << std::endl;
        avcodec_free_context(&codec_context);
        return;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "无法分配视频编码包。" << std::endl;
        avcodec_free_context(&codec_context);
        return;
    }

    int frame_count = 0;
    int encoded_frames = 0;
    AVFrame* frame = nullptr;

    // 主编码循环
    while (video_frame_queue->pop(frame)) {
        if (!frame) {
            break;
        }

        // 确保帧格式正确
        if (frame->format != codec_context->pix_fmt) {
            std::cerr << "警告: 帧格式不匹配，期望 " << codec_context->pix_fmt 
                      << "，实际 " << frame->format << std::endl;
        }

        // 设置正确的时间戳
        frame->pts = frame_count;
        frame->pkt_dts = AV_NOPTS_VALUE;
        
        // 确保帧尺寸正确
        if (frame->width != codec_context->width || frame->height != codec_context->height) {
            std::cerr << "错误: 帧尺寸不匹配" << std::endl;
            av_frame_free(&frame);
            continue;
        }

        frame_count++;

        // 发送帧给编码器
        int ret = avcodec_send_frame(codec_context, frame);
        av_frame_free(&frame);

        if (ret < 0) {
            std::cerr << "发送帧到编码器时出错。" << std::endl;
            continue;
        }

        // 接收编码后的包
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_context, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "编码时出错。" << std::endl;
                break;
            }

            // 复制包并添加到输出队列
            AVPacket* output_packet = av_packet_alloc();
            if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
                encoded_video_queue->push(output_packet);
                encoded_frames++;
            }
            
            av_packet_unref(packet);
        }
    }

    // 刷新编码器
    std::cout << "刷新视频编码器..." << std::endl;
    avcodec_send_frame(codec_context, nullptr);
    
    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }

        AVPacket* output_packet = av_packet_alloc();
        if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
            encoded_video_queue->push(output_packet);
            encoded_frames++;
        }
        
        av_packet_unref(packet);
    }

    // 标记编码完成
    encoded_video_queue->finish();

    // 清理资源
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    
    std::cout << "视频编码线程结束，编码了 " << encoded_frames << " 个包" << std::endl;
}

// 简化编码函数
void video_encode_thread_func_simple(VideoFrameQueue* video_frame_queue, 
                                     EncodedVideoPacketQueue* encoded_video_queue,
                                     int width, int height, int fps) {
    VideoEncoderParams params;
    params.width = width;
    params.height = height;
    params.fps = fps;
    params.codec_id = AV_CODEC_ID_MPEG4;
    params.bitrate = width * height * fps / 10;  // 基于分辨率的比特率估算
    
    video_encode_thread_func(video_frame_queue, encoded_video_queue, params);
}
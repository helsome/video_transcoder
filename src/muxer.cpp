#include "muxer.h"
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/channel_layout.h>
}

void mux_thread_func(EncodedVideoPacketQueue* video_packet_queue,
                     EncodedAudioPacketQueue* audio_packet_queue,
                     const MuxerParams& params) {
    std::cout << "Mux线程已启动，输出文件: " << params.output_filename 
              << " 格式: " << params.format_name << std::endl;

    AVFormatContext* output_format_context = nullptr;
    
    // 分配输出格式上下文，指定AVI格式
    if (avformat_alloc_output_context2(&output_format_context, nullptr, 
                                      params.format_name, params.output_filename) < 0) {
        std::cerr << "无法创建输出格式上下文。" << std::endl;
        return;
    }

    AVStream* video_stream = nullptr;
    AVStream* audio_stream = nullptr;
    int video_stream_index = -1;
    int audio_stream_index = -1;

    // 创建视频流
    if (video_packet_queue) {
        video_stream = avformat_new_stream(output_format_context, nullptr);
        if (!video_stream) {
            std::cerr << "无法创建视频流。" << std::endl;
            avformat_free_context(output_format_context);
            return;
        }
        video_stream_index = video_stream->index;

        // 视频流参数
        video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        video_stream->codecpar->codec_id = params.video_codec_id;
        video_stream->codecpar->width = params.video_width;
        video_stream->codecpar->height = params.video_height;
        video_stream->codecpar->format = AV_PIX_FMT_YUV420P;
        video_stream->codecpar->bit_rate = 800000; // 800kbps
        video_stream->time_base = {1, params.video_fps};
        
        std::cout << "创建视频流: " << params.video_width << "x" << params.video_height 
                  << " 编码器: " << avcodec_get_name(params.video_codec_id) << std::endl;
    }

    // 创建音频流
    if (audio_packet_queue) {
        audio_stream = avformat_new_stream(output_format_context, nullptr);
        if (!audio_stream) {
            std::cerr << "无法创建音频流。" << std::endl;
            avformat_free_context(output_format_context);
            return;
        }
        audio_stream_index = audio_stream->index;

        // 音频流参数
        audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_stream->codecpar->codec_id = params.audio_codec_id;
        audio_stream->codecpar->sample_rate = params.audio_sample_rate;
        av_channel_layout_default(&audio_stream->codecpar->ch_layout, params.audio_channels);
        audio_stream->codecpar->format = AV_SAMPLE_FMT_FLTP;
        audio_stream->codecpar->bit_rate = 128000; // 128kbps
        audio_stream->time_base = {1, params.audio_sample_rate};
        
        std::cout << "创建音频流: " << params.audio_sample_rate << "Hz, " 
                  << params.audio_channels << " 声道, 编码器: " 
                  << avcodec_get_name(params.audio_codec_id) << std::endl;
    }

    // 打开输出文件
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_format_context->pb, params.output_filename, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "无法打开输出文件: " << params.output_filename << std::endl;
            avformat_free_context(output_format_context);
            return;
        }
    }

    // 写入文件头
    if (avformat_write_header(output_format_context, nullptr) < 0) {
        std::cerr << "写入文件头失败。" << std::endl;
        if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_format_context->pb);
        }
        avformat_free_context(output_format_context);
        return;
    }

    bool video_done = (video_packet_queue == nullptr);
    bool audio_done = (audio_packet_queue == nullptr);
    int64_t video_pts = 0;
    int64_t audio_pts = 0;
    int video_packet_count = 0;
    int audio_packet_count = 0;

    // 主循环：从队列中获取包并写入文件
    while (!video_done || !audio_done) {
        AVPacket* packet = nullptr;
        int stream_index = -1;
        bool is_video = false;

        // 选择下一个要写的包（基于时间戳）
        if (!video_done && !audio_done) {
            if (video_pts <= audio_pts) {
                is_video = true;
            } else {
                is_video = false;
            }
        } else if (!video_done) {
            is_video = true;
        } else {
            is_video = false;
        }

        if (is_video && !video_done) {
            if (video_packet_queue->pop(packet)) {
                stream_index = video_stream_index;
                video_packet_count++;
            } else {
                video_done = true;
                continue;
            }
        } else if (!is_video && !audio_done) {
            if (audio_packet_queue->pop(packet)) {
                stream_index = audio_stream_index;
                audio_packet_count++;
            } else {
                audio_done = true;
                continue;
            }
        }

        if (packet) {
            packet->stream_index = stream_index;

            // 时间戳处理
            AVStream* stream = output_format_context->streams[stream_index];
            
            if (packet->pts == AV_NOPTS_VALUE) {
                if (stream_index == video_stream_index) {
                    packet->pts = video_packet_count;
                    packet->dts = packet->pts;
                } else {
                    packet->pts = audio_packet_count;
                    packet->dts = packet->pts;
                }
            }
            
            // 时间戳缩放
            if (stream_index == video_stream_index) {
                AVRational frame_rate = {params.video_fps, 1};
                av_packet_rescale_ts(packet, av_inv_q(frame_rate), stream->time_base);
            } else {
                AVRational sample_rate = {1, params.audio_sample_rate};
                av_packet_rescale_ts(packet, sample_rate, stream->time_base);
            }

            // 更新PTS
            if (stream_index == video_stream_index) {
                video_pts = packet->pts;
            } else {
                audio_pts = packet->pts;
            }

            // 写入包
            if (av_interleaved_write_frame(output_format_context, packet) < 0) {
                std::cerr << "写入包失败。" << std::endl;
            }

            av_packet_free(&packet);
        }
    }

    // 写入文件尾
    av_write_trailer(output_format_context);

    // 清理资源
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);

    std::cout << "Mux线程已结束，输出文件已生成: " << params.output_filename << std::endl;
    std::cout << "写入了 " << video_packet_count << " 个视频包和 " 
              << audio_packet_count << " 个音频包" << std::endl;
}

// 简化封装函数
void mux_thread_func_simple(EncodedVideoPacketQueue* video_packet_queue,
                           EncodedAudioPacketQueue* audio_packet_queue,
                           const char* output_filename,
                           int video_width, int video_height, int video_fps,
                           int audio_sample_rate, int audio_channels) {
    MuxerParams params;
    params.output_filename = output_filename;
    params.format_name = "avi";
    params.video_width = video_width;
    params.video_height = video_height;
    params.video_fps = video_fps;
    params.video_codec_id = AV_CODEC_ID_MPEG4;
    params.audio_sample_rate = audio_sample_rate;
    params.audio_channels = audio_channels;
    params.audio_codec_id = AV_CODEC_ID_AC3;
    
    mux_thread_func(video_packet_queue, audio_packet_queue, params);
}

// 视频专用Mux线程函数
void video_mux_thread_func(EncodedVideoPacketQueue* video_packet_queue,
                          const VideoMuxerParams& params) {
    std::cout << "视频Mux线程已启动，输出文件: " << params.output_filename 
              << " 格式: " << params.format_name << std::endl;

    AVFormatContext* output_format_context = nullptr;
    
    // 分配输出格式上下文
    if (avformat_alloc_output_context2(&output_format_context, nullptr, 
                                      params.format_name, params.output_filename) < 0) {
        std::cerr << "错误: 无法创建视频输出格式上下文。" << std::endl;
        return;
    }

    AVStream* video_stream = nullptr;
    int video_stream_index = -1;

    // 创建视频流
    video_stream = avformat_new_stream(output_format_context, nullptr);
    if (!video_stream) {
        std::cerr << "错误: 无法创建视频流。" << std::endl;
        avformat_free_context(output_format_context);
        return;
    }
    video_stream_index = video_stream->index;

    // 设置视频流参数
    video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video_stream->codecpar->codec_id = params.video_codec_id;
    video_stream->codecpar->width = params.video_width;
    video_stream->codecpar->height = params.video_height;
    video_stream->codecpar->format = AV_PIX_FMT_YUV420P;
    video_stream->codecpar->bit_rate = params.bitrate;
    video_stream->time_base = {1, params.video_fps};
    
    std::cout << "创建视频流: " << params.video_width << "x" << params.video_height 
              << " @ " << params.video_fps << "fps, 比特率: " << params.bitrate << std::endl;

    // 打开输出文件
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_format_context->pb, params.output_filename, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "错误: 无法打开视频输出文件 " << params.output_filename << std::endl;
            avformat_free_context(output_format_context);
            return;
        }
    }

    // 写入文件头
    if (avformat_write_header(output_format_context, nullptr) < 0) {
        std::cerr << "错误: 无法写入视频文件头" << std::endl;
        if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_format_context->pb);
        }
        avformat_free_context(output_format_context);
        return;
    }

    // 处理视频包
    AVPacket* packet = nullptr;
    int video_packet_count = 0;
    
    while (video_packet_queue->pop(packet)) {
        if (!packet) {
            // 接收到结束信号
            break;
        }
        
        // 设置流索引
        packet->stream_index = video_stream_index;
        
        // 转换时间戳到视频流的时间基
        // 视频PTS是连续的帧数，需要转换为时间
        AVRational frame_rate = {params.video_fps, 1};
        av_packet_rescale_ts(packet, av_inv_q(frame_rate), video_stream->time_base);
        
        // 写入包
        int ret = av_interleaved_write_frame(output_format_context, packet);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "错误: 写入视频包失败: " << errbuf << std::endl;
        } else {
            video_packet_count++;
        }
        
        // 释放包
        av_packet_free(&packet);
    }

    // 写入文件尾
    if (av_write_trailer(output_format_context) < 0) {
        std::cerr << "错误: 无法写入视频文件尾" << std::endl;
    }

    // 清理资源
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);

    std::cout << "视频Mux线程结束，写入了 " << video_packet_count << " 个视频包到 " 
              << params.output_filename << std::endl;
}

// 音频专用Mux线程函数
void audio_mux_thread_func(EncodedAudioPacketQueue* audio_packet_queue,
                          const AudioMuxerParams& params) {
    std::cout << "音频Mux线程已启动，输出文件: " << params.output_filename 
              << " 格式: " << params.format_name << std::endl;

    AVFormatContext* output_format_context = nullptr;
    
    // 分配输出格式上下文
    if (avformat_alloc_output_context2(&output_format_context, nullptr, 
                                      params.format_name, params.output_filename) < 0) {
        std::cerr << "错误: 无法创建音频输出格式上下文。" << std::endl;
        return;
    }

    AVStream* audio_stream = nullptr;
    int audio_stream_index = -1;

    // 创建音频流
    audio_stream = avformat_new_stream(output_format_context, nullptr);
    if (!audio_stream) {
        std::cerr << "错误: 无法创建音频流。" << std::endl;
        avformat_free_context(output_format_context);
        return;
    }
    audio_stream_index = audio_stream->index;

    // 设置音频流参数
    audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_stream->codecpar->codec_id = params.audio_codec_id;
    audio_stream->codecpar->sample_rate = params.audio_sample_rate;
    
    av_channel_layout_default(&audio_stream->codecpar->ch_layout, params.audio_channels);
    
    // 根据编码器设置采样格式
    if (params.audio_codec_id == AV_CODEC_ID_AC3) {
        audio_stream->codecpar->format = AV_SAMPLE_FMT_FLTP;
    } else {
        audio_stream->codecpar->format = AV_SAMPLE_FMT_FLTP;
    }
    
    audio_stream->codecpar->bit_rate = params.bitrate;
    audio_stream->time_base = {1, params.audio_sample_rate};
    
    std::cout << "创建音频流: " << params.audio_sample_rate << "Hz, " 
              << params.audio_channels << "通道, 比特率: " << params.bitrate 
              << ", 编码器: " << avcodec_get_name(params.audio_codec_id) << std::endl;

    // 打开输出文件
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_format_context->pb, params.output_filename, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "错误: 无法打开音频输出文件 " << params.output_filename << std::endl;
            avformat_free_context(output_format_context);
            return;
        }
    }

    // 写入文件头
    if (avformat_write_header(output_format_context, nullptr) < 0) {
        std::cerr << "错误: 无法写入音频文件头" << std::endl;
        if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_format_context->pb);
        }
        avformat_free_context(output_format_context);
        return;
    }

    // 处理音频包
    AVPacket* packet = nullptr;
    int audio_packet_count = 0;
    
    while (audio_packet_queue->pop(packet)) {
        if (!packet) {
            // 接收到结束信号
            break;
        }
        
        // 设置流索引
        packet->stream_index = audio_stream_index;
        
        // 转换时间戳到音频流的时间基
        av_packet_rescale_ts(packet, {1, params.audio_sample_rate}, audio_stream->time_base);
        
        // 写入包
        int ret = av_interleaved_write_frame(output_format_context, packet);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "错误: 写入音频包失败: " << errbuf << std::endl;
        } else {
            audio_packet_count++;
        }
        
        // 释放包
        av_packet_free(&packet);
    }

    // 写入文件尾
    if (av_write_trailer(output_format_context) < 0) {
        std::cerr << "错误: 无法写入音频文件尾" << std::endl;
    }

    // 清理资源
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);

    std::cout << "音频Mux线程结束，写入了 " << audio_packet_count << " 个音频包到 " 
              << params.output_filename << std::endl;
}
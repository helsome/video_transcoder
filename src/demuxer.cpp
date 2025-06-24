#include "demuxer.h"
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

bool get_stream_info(const char* input_filename, StreamInfo& info) {
    AVFormatContext* format_context = nullptr;
    
    if (avformat_open_input(&format_context, input_filename, nullptr, nullptr) != 0) {
        std::cerr << "错误：无法打开输入文件 " << input_filename << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        std::cerr << "错误：无法查找流信息。" << std::endl;
        avformat_close_input(&format_context);
        return false;
    }
    
    // 找到视频和音频流
    for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
        AVStream* stream = format_context->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && info.video_stream_index == -1) {
            info.video_stream_index = i;
            info.video_width = stream->codecpar->width;
            info.video_height = stream->codecpar->height;
            info.video_pixel_format = (AVPixelFormat)stream->codecpar->format;
            
            // 计算帧率
            if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
                info.video_fps = stream->r_frame_rate.num / stream->r_frame_rate.den;
            }
            
            // 复制编解码器参数
            info.video_codec_params = avcodec_parameters_alloc();
            avcodec_parameters_copy(info.video_codec_params, stream->codecpar);
            
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && info.audio_stream_index == -1) {
            info.audio_stream_index = i;
            info.audio_sample_rate = stream->codecpar->sample_rate;
            info.audio_channels = stream->codecpar->ch_layout.nb_channels;  // 使用新的API
            info.audio_sample_format = (AVSampleFormat)stream->codecpar->format;
            
            // 复制编解码器参数
            info.audio_codec_params = avcodec_parameters_alloc();
            avcodec_parameters_copy(info.audio_codec_params, stream->codecpar);
        }
    }
    
    avformat_close_input(&format_context);
    
    std::cout << "流信息获取成功:" << std::endl;
    std::cout << "  视频: " << info.video_width << "x" << info.video_height 
              << " @ " << info.video_fps << "fps" << std::endl;
    std::cout << "  音频: " << info.audio_sample_rate << "Hz, " 
              << info.audio_channels << " 声道" << std::endl;
    
    return (info.video_stream_index >= 0 || info.audio_stream_index >= 0);
}

void demux_thread_func(const char* input_filename,
                       VideoPacketQueue* video_packet_queue,
                       AudioPacketQueue* audio_packet_queue) {
    DemuxerParams params;
    params.input_filename = input_filename;
    demux_thread_func_with_params(params, video_packet_queue, audio_packet_queue);
}

void demux_thread_func_with_params(const DemuxerParams& params,
                                  VideoPacketQueue* video_packet_queue,
                                  AudioPacketQueue* audio_packet_queue) {
    std::cout << "解封装线程已启动，文件: " << params.input_filename << std::endl;
    
    AVFormatContext* format_context = nullptr;
    int video_stream_index = -1;
    int audio_stream_index = -1;
    
    // 打开输入文件并分配上下文
    if (avformat_open_input(&format_context, params.input_filename, nullptr, nullptr) != 0) {
        std::cerr << "错误：无法打开输入文件 " << params.input_filename << std::endl;
        return;
    }
    
    //  查找流信息
    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        std::cerr << "错误：无法查找流信息。" << std::endl;
        avformat_close_input(&format_context);
        return;
    }
    
    //  找到视频和音频流的索引
    for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && params.enable_video) {
            video_stream_index = i;
        } else if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && params.enable_audio) {
            audio_stream_index = i;
        }
    }
    
    if (video_stream_index == -1 && audio_stream_index == -1) {
        std::cerr << "错误：未找到有效的视频流或音频流。" << std::endl;
        avformat_close_input(&format_context);
        return;
    }
    
    std::cout << "视频流索引: " << video_stream_index << std::endl;
    std::cout << "音频流索引: " << audio_stream_index << std::endl;
    
    //  循环读取数据包
    AVPacket* packet = av_packet_alloc();
    int video_frame_count = 0;
    int audio_frame_count = 0;
    
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == video_stream_index && video_packet_queue) {
            AVPacket* video_packet = av_packet_alloc();
            av_packet_ref(video_packet, packet);
            video_packet_queue->push(video_packet);
            video_frame_count++;
        } else if (packet->stream_index == audio_stream_index && audio_packet_queue) {
            AVPacket* audio_packet = av_packet_alloc();
            av_packet_ref(audio_packet, packet);
            audio_packet_queue->push(audio_packet);
            audio_frame_count++;
        }
        
        av_packet_unref(packet);
        
        // 检查是否达到最大视频帧数限制（以视频帧为准进行同步限制）
        if (params.max_frames > 0 && video_frame_count >= params.max_frames) {
            std::cout << "达到最大帧数限制: " << params.max_frames << " (视频帧)" << std::endl;
            break;
        }
    }
    
    // 标记队列完成
    if (video_packet_queue) {
        video_packet_queue->finish();
    }
    if (audio_packet_queue) {
        audio_packet_queue->finish();
    }
    
    av_packet_free(&packet);
    avformat_close_input(&format_context);
    
    std::cout << "解封装完成，处理了 " << video_frame_count << " 个视频帧，" 
              << audio_frame_count << " 个音频帧" << std::endl;
}
#include "audio_encoder.h"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

// =============== AC3编码器实现 ===============
AC3Encoder::~AC3Encoder() {
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
    }
}

bool AC3Encoder::initialize(const AudioEncoderParams& params) {
    params_ = params;
    
    // 查找AC3编码器
    codec_ = avcodec_find_encoder(AV_CODEC_ID_AC3);
    if (!codec_) {
        std::cerr << "未找到AC3编码器" << std::endl;
        return false;
    }

    // 分配编码器上下文
    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_) {
        std::cerr << "无法分配AC3编码器上下文" << std::endl;
        return false;
    }

    // AC3编码器的特殊设置
    codec_context_->bit_rate = params.bitrate;
    codec_context_->sample_rate = params.sample_rate;
    av_channel_layout_default(&codec_context_->ch_layout, params.channels);
    codec_context_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    
    codec_context_->frame_size = 1536;

    // 打开编码器
    if (avcodec_open2(codec_context_, codec_, nullptr) < 0) {
        std::cerr << "无法打开AC3编码器" << std::endl;
        avcodec_free_context(&codec_context_);
        return false;
    }

    std::cout << "AC3编码器初始化成功: " << params.sample_rate << "Hz, " 
              << params.channels << "通道, " << params.bitrate << "bps, 帧大小: " 
              << codec_context_->frame_size << std::endl;
    return true;
}

bool AC3Encoder::encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "无法分配AC3编码包" << std::endl;
        return false;
    }

    // AC3编码器要求固定的frame_size，检查输入帧是否符合要求
    if (frame && frame->nb_samples != codec_context_->frame_size) {
        std::cerr << "AC3编码器要求帧大小为 " << codec_context_->frame_size 
                  << " 但收到 " << frame->nb_samples << " 样本，跳过此帧" << std::endl;
        av_packet_free(&packet);
        return false;
    }

    // 发送帧给编码器
    int ret = avcodec_send_frame(codec_context_, frame);
    if (ret < 0) {
        std::cerr << "AC3编码器发送帧失败" << std::endl;
        av_packet_free(&packet);
        return false;
    }

    // 接收编码后的包
    bool success = true;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "AC3编码接收包失败" << std::endl;
            success = false;
            break;
        }

        // 复制包并添加到输出队列，保持时间戳
        AVPacket* output_packet = av_packet_alloc();
        if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
            // 确保时间戳信息正确传递
            // FFmpeg编码器应该已经根据输入帧的PTS设置了输出包的PTS/DTS
            output_queue->push(output_packet);
        }
        
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return success;
}

bool AC3Encoder::flush(EncodedAudioPacketQueue* output_queue) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }

    // 刷新编码器
    avcodec_send_frame(codec_context_, nullptr);
    
    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }

        AVPacket* output_packet = av_packet_alloc();
        if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
            output_queue->push(output_packet);
        }
        
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return true;
}

// =============== AAC编码器实现 ===============
AACEncoder::~AACEncoder() {
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
    }
}

bool AACEncoder::initialize(const AudioEncoderParams& params) {
    params_ = params;
    
    // 查找AAC编码器
    codec_ = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec_) {
        std::cerr << "未找到AAC编码器" << std::endl;
        return false;
    }

    // 分配编码器上下文
    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_) {
        std::cerr << "无法分配AAC编码器上下文" << std::endl;
        return false;
    }

    // 设置编码参数
    codec_context_->bit_rate = params.bitrate;
    codec_context_->sample_rate = params.sample_rate;
    av_channel_layout_default(&codec_context_->ch_layout, params.channels);
    codec_context_->sample_fmt = AV_SAMPLE_FMT_FLTP;

    if (avcodec_open2(codec_context_, codec_, nullptr) < 0) {
        std::cerr << "无法打开AAC编码器" << std::endl;
        avcodec_free_context(&codec_context_);
        return false;
    }

    std::cout << "AAC编码器初始化成功: " << params.sample_rate << "Hz, " 
              << params.channels << "通道, " << params.bitrate << "bps" << std::endl;
    return true;
}

bool AACEncoder::encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "无法分配AAC编码包" << std::endl;
        return false;
    }

    // 发送帧给编码器
    int ret = avcodec_send_frame(codec_context_, frame);
    if (ret < 0) {
        std::cerr << "AAC编码器发送帧失败" << std::endl;
        av_packet_free(&packet);
        return false;
    }

    // 接收编码后的包
    bool success = true;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "AAC编码接收包失败" << std::endl;
            success = false;
            break;
        }

        // 复制包并添加到输出队列
        AVPacket* output_packet = av_packet_alloc();
        if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
            output_queue->push(output_packet);
        }
        
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return success;
}

bool AACEncoder::flush(EncodedAudioPacketQueue* output_queue) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }

    // 刷新编码器
    avcodec_send_frame(codec_context_, nullptr);
    
    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }

        AVPacket* output_packet = av_packet_alloc();
        if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
            output_queue->push(output_packet);
        }
        
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return true;
}

// =============== MP3编码器实现 ===============
MP3Encoder::~MP3Encoder() {
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
    }
}

bool MP3Encoder::initialize(const AudioEncoderParams& params) {
    params_ = params;
    
    // 查找MP3编码器
    codec_ = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!codec_) {
        std::cerr << "未找到MP3编码器" << std::endl;
        return false;
    }

    // 分配编码器上下文
    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_) {
        std::cerr << "无法分配MP3编码器上下文" << std::endl;
        return false;
    }

    // 设置编码参数
    codec_context_->bit_rate = params.bitrate;
    codec_context_->sample_rate = params.sample_rate;
    av_channel_layout_default(&codec_context_->ch_layout, params.channels);
    codec_context_->sample_fmt = AV_SAMPLE_FMT_FLTP;

    if (avcodec_open2(codec_context_, codec_, nullptr) < 0) {
        std::cerr << "无法打开MP3编码器" << std::endl;
        avcodec_free_context(&codec_context_);
        return false;
    }

    std::cout << "MP3编码器初始化成功: " << params.sample_rate << "Hz, " 
              << params.channels << "通道, " << params.bitrate << "bps" << std::endl;
    return true;
}

bool MP3Encoder::encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "无法分配MP3编码包" << std::endl;
        return false;
    }

    // 发送帧给编码器
    int ret = avcodec_send_frame(codec_context_, frame);
    if (ret < 0) {
        std::cerr << "MP3编码器发送帧失败" << std::endl;
        av_packet_free(&packet);
        return false;
    }

    // 接收编码后的包
    bool success = true;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "MP3编码接收包失败" << std::endl;
            success = false;
            break;
        }

        // 复制包并添加到输出队列
        AVPacket* output_packet = av_packet_alloc();
        if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
            output_queue->push(output_packet);
        }
        
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return success;
}

bool MP3Encoder::flush(EncodedAudioPacketQueue* output_queue) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }

    // 刷新编码器
    avcodec_send_frame(codec_context_, nullptr);
    
    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }

        AVPacket* output_packet = av_packet_alloc();
        if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
            output_queue->push(output_packet);
        }
        
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return true;
}

// =============== 复制编码器实现 ===============
bool CopyEncoder::initialize(const AudioEncoderParams& params) {
    params_ = params;
    std::cout << "复制编码器初始化" << std::endl;
    return true;
}

bool CopyEncoder::encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) {
    // 在复制模式下，将帧转换为包
    std::cerr << "警告: 复制编码器暂不支持AVFrame输入，请使用packet级别的复制" << std::endl;
    return false;
}

bool CopyEncoder::flush(EncodedAudioPacketQueue* output_queue) {
    // 复制模式无需刷新
    return true;
}

// =============== 工厂函数实现===============
std::unique_ptr<IAudioEncoder> create_audio_encoder(TargetAudioFormat format) {
    switch (format) {
        case TargetAudioFormat::AC3:
            return std::make_unique<AC3Encoder>();
        case TargetAudioFormat::AAC:
            return std::make_unique<AACEncoder>();
        case TargetAudioFormat::MP3:
            return std::make_unique<MP3Encoder>();
        case TargetAudioFormat::COPY:
            return std::make_unique<CopyEncoder>();
        default:
            std::cerr << "错误: 不支持的音频格式" << std::endl;
            return nullptr;
    }
}

// 音频编码线程函数
void audio_encode_thread_func_factory(AudioFrameQueue* audio_frame_queue, 
                                      EncodedAudioPacketQueue* encoded_audio_queue,
                                      TargetAudioFormat target_format,
                                      const AudioEncoderParams& params) {
    std::cout << "音频编码线程（工厂模式）已启动" << std::endl;
    
    auto encoder = create_audio_encoder(target_format);
    if (!encoder) {
        std::cerr << "错误: 无法创建音频编码器" << std::endl;
        return;
    }
    
    // 初始化编码器
    if (!encoder->initialize(params)) {
        std::cerr << "错误: 音频编码器初始化失败" << std::endl;
        return;
    }
    
    std::cout << "使用编码器: " << encoder->get_encoder_name() << std::endl;
    
    int frame_count = 0;
    int encoded_frames = 0;
    AVFrame* frame = nullptr;

    // 主编码循环
    while (audio_frame_queue->pop(frame)) {
        if (!frame) {
            break;
        }

        if (encoder->encode_frame(frame, encoded_audio_queue)) {
            encoded_frames++;
        }
        
        av_frame_free(&frame);
        frame_count++;
    }

    // 刷新编码器
    std::cout << "刷新音频编码器 (" << encoder->get_encoder_name() << ")..." << std::endl;
    encoder->flush(encoded_audio_queue);

    // 标记编码完成
    encoded_audio_queue->finish();
    
    std::cout << "音频编码线程（工厂模式）结束，使用 " << encoder->get_encoder_name() 
              << " 编码了 " << encoded_frames << " 个包" << std::endl;
}

// 传统音频编码线程函数
void audio_encode_thread_func(AudioFrameQueue* audio_frame_queue, 
                              EncodedAudioPacketQueue* encoded_audio_queue,
                              const AudioEncoderParams& params) {
    std::cout << "音频编码线程已启动，使用编码器: " << avcodec_get_name(params.codec_id) << std::endl;
    
    const AVCodec* codec = avcodec_find_encoder(params.codec_id);
    if (!codec) {
        std::cerr << "未找到音频编码器，ID: " << params.codec_id << std::endl;
        return;
    }

    // 分配编码器上下文
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "无法分配音频编码器上下文。" << std::endl;
        return;
    }

    codec_context->bit_rate = params.bitrate;
    codec_context->sample_rate = params.sample_rate;
    
    av_channel_layout_default(&codec_context->ch_layout, params.channels);
    
    codec_context->sample_fmt = params.sample_format;

    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "无法打开音频编码器。" << std::endl;
        avcodec_free_context(&codec_context);
        return;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "无法分配音频编码包。" << std::endl;
        avcodec_free_context(&codec_context);
        return;
    }

    int frame_count = 0;
    int encoded_frames = 0;
    AVFrame* frame = nullptr;

    // 主编码循环
    while (audio_frame_queue->pop(frame)) {
        if (!frame) {
            break;
        }

        // 发送帧给编码器
        int ret = avcodec_send_frame(codec_context, frame);
        av_frame_free(&frame);

        if (ret < 0) {
            std::cerr << "发送音频帧到编码器时出错。" << std::endl;
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_context, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "音频编码时出错。" << std::endl;
                break;
            }

            AVPacket* output_packet = av_packet_alloc();
            if (output_packet && av_packet_ref(output_packet, packet) >= 0) {
                encoded_audio_queue->push(output_packet);
                encoded_frames++;
            }
            
            av_packet_unref(packet);
        }
        
        frame_count++;
    }

    // 刷新编码器
    std::cout << "刷新音频编码器..." << std::endl;
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
            encoded_audio_queue->push(output_packet);
            encoded_frames++;
        }
        
        av_packet_unref(packet);
    }

    // 标记编码完成
    encoded_audio_queue->finish();

    // 清理资源
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    
    std::cout << "音频编码线程结束，编码了 " << encoded_frames << " 个包" << std::endl;
}

// 简化编码函数
void audio_encode_thread_func_simple(AudioFrameQueue* audio_frame_queue, 
                                     EncodedAudioPacketQueue* encoded_audio_queue,
                                     int sample_rate, int channels) {
    AudioEncoderParams params;
    params.sample_rate = sample_rate;
    params.channels = channels;
    params.codec_id = AV_CODEC_ID_AC3;
    params.bitrate = 128000;
    
    audio_encode_thread_func(audio_frame_queue, encoded_audio_queue, params);
}
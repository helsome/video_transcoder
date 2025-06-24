#pragma once

#include "queue.h"
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 目标音频格式枚举（编码规则要求）
enum class TargetAudioFormat {
    AAC,    // AAC编码（默认，更兼容）
    AC3,    // AC3编码
    MP3,    // MP3编码
    COPY    // 复制原始格式（透传）
};

// 音频编码器配置参数
struct AudioEncoderParams {
    int sample_rate = 48000;
    int channels = 2;
    int bitrate = 128000;  // 128kbps
    AVCodecID codec_id = AV_CODEC_ID_AC3;  // 默认AC3
    AVSampleFormat sample_format = AV_SAMPLE_FMT_FLTP;
    uint64_t channel_layout = AV_CH_LAYOUT_STEREO;
};

// 音频编码器抽象基类接口（编码规则强制要求）
class IAudioEncoder {
public:
    virtual ~IAudioEncoder() = default;
    
    // 初始化编码器
    virtual bool initialize(const AudioEncoderParams& params) = 0;
    
    // 编码单个音频帧
    virtual bool encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) = 0;
    
    // 刷新编码器（获取延迟的包）
    virtual bool flush(EncodedAudioPacketQueue* output_queue) = 0;
    
    // 获取编码器信息
    virtual const char* get_encoder_name() const = 0;
    virtual AVCodecID get_codec_id() const = 0;
    
protected:
    AudioEncoderParams params_;
    AVCodecContext* codec_context_ = nullptr;
    const AVCodec* codec_ = nullptr;
};

// AC3编码器实现（默认）
class AC3Encoder : public IAudioEncoder {
public:
    AC3Encoder() = default;
    ~AC3Encoder() override;
    
    bool initialize(const AudioEncoderParams& params) override;
    bool encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) override;
    bool flush(EncodedAudioPacketQueue* output_queue) override;
    const char* get_encoder_name() const override { return "AC3 Encoder"; }
    AVCodecID get_codec_id() const override { return AV_CODEC_ID_AC3; }
};

// AAC编码器实现
class AACEncoder : public IAudioEncoder {
public:
    AACEncoder() = default;
    ~AACEncoder() override;
    
    bool initialize(const AudioEncoderParams& params) override;
    bool encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) override;
    bool flush(EncodedAudioPacketQueue* output_queue) override;
    const char* get_encoder_name() const override { return "AAC Encoder"; }
    AVCodecID get_codec_id() const override { return AV_CODEC_ID_AAC; }
};

// MP3编码器实现
class MP3Encoder : public IAudioEncoder {
public:
    MP3Encoder() = default;
    ~MP3Encoder() override;
    
    bool initialize(const AudioEncoderParams& params) override;
    bool encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) override;
    bool flush(EncodedAudioPacketQueue* output_queue) override;
    const char* get_encoder_name() const override { return "MP3 Encoder"; }
    AVCodecID get_codec_id() const override { return AV_CODEC_ID_MP3; }
};

// 复制编码器实现（透传原始格式）
class CopyEncoder : public IAudioEncoder {
public:
    CopyEncoder() = default;
    ~CopyEncoder() override = default;
    
    bool initialize(const AudioEncoderParams& params) override;
    bool encode_frame(AVFrame* frame, EncodedAudioPacketQueue* output_queue) override;
    bool flush(EncodedAudioPacketQueue* output_queue) override;
    const char* get_encoder_name() const override { return "Copy Encoder"; }
    AVCodecID get_codec_id() const override { return params_.codec_id; }
};

// 音频编码器工厂函数（编码规则强制要求）
std::unique_ptr<IAudioEncoder> create_audio_encoder(TargetAudioFormat format);

// 新的基于工厂模式的音频编码线程函数
void audio_encode_thread_func_factory(AudioFrameQueue* audio_frame_queue, 
                                      EncodedAudioPacketQueue* encoded_audio_queue,
                                      TargetAudioFormat target_format,
                                      const AudioEncoderParams& params);

// 原有的音频编码线程函数（保持向后兼容）
void audio_encode_thread_func(AudioFrameQueue* audio_frame_queue, 
                              EncodedAudioPacketQueue* encoded_audio_queue,
                              const AudioEncoderParams& params);

// 便利函数：使用默认参数编码
void audio_encode_thread_func_simple(AudioFrameQueue* audio_frame_queue, 
                                     EncodedAudioPacketQueue* encoded_audio_queue,
                                     int sample_rate, int channels);
#pragma once

#include "queue.h"
#include <memory>
#include <vector>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

// 前向声明SoundTouch类
namespace soundtouch {
    class SoundTouch;
}

// 音频处理参数
struct AudioProcessParams {
    // 音量调整
    double volume_gain = 1.0;  // 1.0表示不变，0.5表示减半，2.0表示加倍
    
    // 重采样参数
    bool enable_resample = false;
    int target_sample_rate = 48000;
    int target_channels = 2;
    
    // 滤波器效果
    bool enable_lowpass = false;
    int lowpass_frequency = 8000;  // 低通滤波器截止频率
    
    bool enable_highpass = false;
    int highpass_frequency = 200;  // 高通滤波器截止频率
    
    // 动态范围压缩
    bool enable_compressor = false;
    double threshold = -20.0;     // 压缩阈值(dB)
    double ratio = 4.0;           // 压缩比例
    double attack = 5.0;          // 攻击时间(ms)
    double release = 50.0;        // 释放时间(ms)
    
    // 降噪
    bool enable_noise_reduction = false;
    double noise_reduction_strength = 0.5;  // 0.0-1.0
    
    // 音频变速参数（新增）
    bool enable_speed_change = false;
    double speed_factor = 1.0;    // 变速倍数，1.0表示正常速度，2.0表示2倍速，0.5表示半速
};

// 环形缓冲区类（用于处理音频数据流和固定frame_size需求）
class AudioRingBuffer {
public:
    AudioRingBuffer(int frame_size, int channels, int sample_rate);
    ~AudioRingBuffer();
    
    // 向缓冲区写入样本数据
    bool write_samples(const float* samples, int num_samples);
    
    // 从缓冲区读取固定数量的样本（frame_size）
    bool read_frame(float* output_samples, int& actual_samples);
    
    // 获取可读样本数
    int available_samples() const;
    
    // 是否有足够的样本组成一个完整帧
    bool has_complete_frame() const;
    
    // 清空缓冲区
    void clear();
    
    // 获取内部缓冲区数据（用于flush时处理剩余数据）
    const std::vector<float>& get_buffer() const { return buffer_; }
    int get_read_pos() const { return read_pos_; }

private:
    std::vector<float> buffer_;
    int frame_size_;      // 每帧样本数（如AC3的1536）
    int channels_;
    int sample_rate_;
    
    int write_pos_;       // 写入位置
    int read_pos_;        // 读取位置
    int available_;       // 可用样本数
    
    mutable std::mutex mutex_;
    
    friend class AudioProcessor;  // 允许AudioProcessor访问私有成员
};

// 音频处理器类
class AudioProcessor {
public:
    AudioProcessor();
    ~AudioProcessor();
    
    // 初始化音频处理器
    bool initialize(const AudioProcessParams& params, 
                   int input_sample_rate, 
                   int input_channels,
                   AVSampleFormat input_format);
    
    // 处理音频帧
    bool process_frame(AVFrame* input_frame, AudioFrameQueue* output_queue);
    
    // 刷新处理器
    bool flush(AudioFrameQueue* output_queue);
    
    // 清理资源
    void cleanup();

private:
    AudioProcessParams params_;
    
    // FFmpeg滤波器相关
    AVFilterGraph* filter_graph_;
    AVFilterContext* buffer_src_ctx_;
    AVFilterContext* buffer_sink_ctx_;
    AVFrame* filter_frame_;
    
    // 音频参数
    int input_sample_rate_;
    int input_channels_;
    AVSampleFormat input_format_;
    
    // 音频变速相关（新增）
    std::unique_ptr<soundtouch::SoundTouch> sound_touch_;
    std::unique_ptr<AudioRingBuffer> ring_buffer_;
    std::vector<float> temp_buffer_;
    bool speed_processing_enabled_;
    
    // 时间戳处理
    int64_t last_input_pts_;
    int64_t processed_samples_count_;
    int64_t first_input_pts_;  // 第一个输入帧的时间戳，作为时间基准
    
    // 内部函数
    bool setup_filter_graph();
    std::string build_filter_description();
    
    // 音频变速相关内部函数
    bool initialize_speed_processing();
    bool process_frame_with_speed(AVFrame* input_frame, AudioFrameQueue* output_queue);
    bool process_samples_through_soundtouch(const float* input_samples, int num_samples, AudioFrameQueue* output_queue);
    bool process_samples_through_soundtouch_with_frame_pts(const float* input_samples, int num_samples, int64_t input_pts, AudioFrameQueue* output_queue);
    AVFrame* create_output_frame(const float* samples, int num_samples, int64_t pts);
    
    // 时间戳计算（严格遵循 new_pts = original_pts / speed_factor）
    int64_t calculate_new_pts(int64_t original_pts) const;
};

// 音频处理线程函数
void audio_process_thread_func(AudioFrameQueue* input_frame_queue,
                              AudioFrameQueue* output_frame_queue, 
                              const AudioProcessParams& params,
                              int sample_rate, int channels, 
                              AVSampleFormat format);

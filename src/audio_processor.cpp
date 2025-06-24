/**
 * =====================================================================================
 * 音频处理器模块 (audio_processor.cpp) - 高质量音频变速不变调引擎
 * =====================================================================================
 * 
 * [整体概述]
 * 这是整个转码系统的"音频工作站"，负责高质量的音频变速不变调处理。
 * 在项目架构中，它属于音频信号处理层——类似于专业音频软件(如Audacity)的核心引擎，
 * 但专门针对实时流处理进行了优化。
 * 
 * 架构定位：
 * - 数字信号处理层：音频波形的时域和频域操作
 * - 实时流处理层：连续音频数据的高效处理
 * - 音质保护层：确保变速过程中音调和音质不受损
 * 
 * 交互关系：
 * - 上游：接收音频解码线程的PCM音频帧
 * - 下游：输出处理后的音频帧给音频编码线程
 * - 核心依赖：SoundTouch库(变速不变调)、FFmpeg音频滤镜
 * - 同步要求：与视频处理保持严格的时间同步
 * 
 * [技术重点解析]
 * 
 * **核心算法：WSOLA(Waveform Similarity Overlap-Add)变速不变调**
 * SoundTouch库实现的技术栈：
 * 1. 时域拉伸/压缩：调整音频时间长度
 * 2. 音调补偿：通过重采样恢复原始音调
 * 3. 重叠相加：消除处理过程中的音频伪影
 * 4. 实时处理：支持流式音频的低延迟处理
 * 
 * **关键数据结构：环形缓冲区(AudioRingBuffer)**
 * 解决问题：
 * - SoundTouch输出样本数不固定，但编码器需要固定帧大小
 * - 减少内存分配/释放开销，提高实时性能
 * - 平滑处理音频流的连续性，避免爆音和断断续续
 * 
 * **设计模式应用：**
 * 1. 生产者-消费者模式：SoundTouch作为生产者，编码器作为消费者
 * 2. 适配器模式：环形缓冲区适配不同的输入/输出帧大小
 * 3. 状态模式：音频处理器的多种工作状态管理
 * 4. 策略模式：不同音质设置对应不同的处理策略
 * 
 * **内存管理优化：**
 * - 环形缓冲区：避免频繁的内存分配，提高cache命中率
 * - 浮点PCM格式：提供更高的音频处理精度
 * - 批量处理：减少函数调用开销，提高处理效率
 * 
 * ["答辩"视角下的潜在问题]
 * 
 * **设计抉择分析：**
 * Q: 为什么选择SoundTouch而不是其他音频处理库？
 * A: 
 * - 当前方案：SoundTouch + WSOLA算法
 *   优点：音质优秀、开源免费、API简单、久经考验
 *   缺点：功能相对单一、不支持实时pitch shifting
 * 
 * - 替代方案1：FFTW + 自实现算法
 *   优点：灵活性极高、可定制优化、学术价值高
 *   缺点：开发周期长、音质调优困难、bug风险高
 * 
 * - 替代方案2：商业库(如DIRAC)
 *   优点：音质顶级、功能全面、技术支持好
 *   缺点：授权费用高、开源项目不适用
 * 
 * - 替代方案3：简单重采样
 *   优点：实现简单、CPU消耗低
 *   缺点：音调会改变、音质损失严重
 * 
 * 选择SoundTouch基于：
 * 1. 开源项目的成本考虑
 * 2. 音质与复杂度的平衡
 * 3. 社区支持和文档完善程度
 * 
 * **潜在风险点：**
 * 1. 音频延迟累积：变速处理引入的时间延迟
 *    - 风险场景：实时应用中延迟过大影响用户体验
 *    - 具体数字：WSOLA算法典型延迟20-50ms，不适合实时对话
 *    - 缓解策略：预处理模式、延迟补偿算法
 * 
 * 2. 音质损失：多次变速处理的累积误差
 *    - 风险场景：频繁变速或极端倍率(>3x或<0.3x)
 *    - 缓解策略：高精度中间格式、质量检测机制
 * 
 * 3. 内存泄漏：环形缓冲区的不当使用
 *    - 风险场景：长时间运行时缓冲区无限增长
 *    - 缓解策略：缓冲区大小限制、定期清理机制
 * 
 * 4. 音画同步丢失：与视频处理的时间差异
 *    - 风险场景：音频变速与视频变速的微小差异累积
 *    - 缓解策略：统一时间戳管理、定期同步校正
 */

#include "audio_processor.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

// SoundTouch：开源音频变速不变调库
#include <SoundTouch.h>

using namespace soundtouch;

/**
 * =====================================================================================
 * AudioRingBuffer类：高效环形缓冲区实现
 * =====================================================================================
 * 
 * [核心算法]
 * 环形缓冲区(Circular Buffer)：经典的流数据处理结构
 * 
 * 原理：
 * - 固定大小的数组 + 读写指针
 * - 指针到达数组末尾时自动回绕到开头
 * - 可用空间 = 写指针 - 读指针(考虑回绕)
 * 
 * 优势：
 * 1. 零内存分配：一次分配，长期使用
 * 2. 高缓存效率：连续内存访问模式
 * 3. 线程安全：内置互斥锁保护
 * 4. 低延迟：O(1)读写复杂度
 */

/**
 * 构造函数：初始化环形缓冲区
 * 
 * @param frame_size: 每帧样本数(如1024)
 * @param channels: 声道数(1=单声道, 2=立体声)
 * @param sample_rate: 采样率(如44100Hz)
 * 
 * 缓冲区大小设计：
 * - 4倍帧大小：平衡内存使用与处理平滑性
 * - 考虑最坏情况：SoundTouch可能输出比输入多3倍的样本
 */
// AudioRingBuffer实现
AudioRingBuffer::AudioRingBuffer(int frame_size, int channels, int sample_rate)
    : frame_size_(frame_size)           // 每帧样本数
    , channels_(channels)               // 声道数  
    , sample_rate_(sample_rate)         // 采样率
    , write_pos_(0)                     // 写指针
    , read_pos_(0)                      // 读指针
    , available_(0) {                   // 可用样本数
    
    /**
     * 缓冲区大小计算：
     * frame_size * channels * 4
     * 
     * 例如：1024样本 × 2声道 × 4倍 = 8192浮点数 ≈ 32KB
     * 这个大小可以缓存约86ms的CD质量音频(44.1kHz)
     */
    int buffer_size = frame_size_ * channels_ * 4;
    buffer_.resize(buffer_size, 0.0f);   // 初始化为静音
}

AudioRingBuffer::~AudioRingBuffer() {
    // 析构函数：std::vector自动释放内存，无需手动清理
}

/**
 * 写入音频样本：生产者接口
 * 
 * @param samples: 输入音频样本数组(交错格式)
 * @param num_samples: 每声道样本数
 * @return bool: 成功返回true，缓冲区满返回false
 * 
 * 数据格式：交错PCM (LRLRLR...)
 * L = 左声道样本, R = 右声道样本
 */

bool AudioRingBuffer::write_samples(const float* samples, int num_samples) {
    std::lock_guard<std::mutex> lock(mutex_);  // RAII线程安全
    
    int total_samples = num_samples * channels_;  // 总样本数
    int buffer_size = static_cast<int>(buffer_.size());
    
    /**
     * 容量检查：防止缓冲区溢出
     * 溢出策略：拒绝写入并报错(可改为覆盖旧数据)
     */
    if (available_ + total_samples > buffer_size) {
        std::cerr << "环形缓冲区空间不足" << std::endl;
        return false;
    }
    
    /**
     * 数据写入：逐样本拷贝，自动处理回绕
     * 优化空间：可使用memcpy批量拷贝提高效率
     */
    for (int i = 0; i < total_samples; ++i) {
        buffer_[write_pos_] = samples[i];
        write_pos_ = (write_pos_ + 1) % buffer_size;  // 模运算实现回绕
    }
    
    available_ += total_samples;  // 更新可用样本计数
    return true;
}

/**
 * 读取音频帧：消费者接口
 * 
 * @param output_samples: 输出音频样本缓冲区
 * @param actual_samples: 实际读取的样本数(输出参数)
 * @return bool: 成功读取完整帧返回true，数据不足返回false
 * 
 * 读取策略：全有或全无
 * 只有缓冲区包含完整帧时才进行读取，保证输出帧的完整性
 */
bool AudioRingBuffer::read_frame(float* output_samples, int& actual_samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int frame_total_samples = frame_size_ * channels_;  // 完整帧的样本数
    
    /**
     * 数据充足性检查：确保有完整帧可读
     * 这种设计保证了输出的音频帧大小固定，便于后续编码
     */
    if (available_ < frame_total_samples) {
        actual_samples = 0;
        return false;  // 数据不足，等待更多输入
    }
    
    int buffer_size = static_cast<int>(buffer_.size());
    
    /**
     * 数据读取：从读指针开始提取完整帧
     * 读取后自动移动读指针，为下次读取准备
     */
    for (int i = 0; i < frame_total_samples; ++i) {
        output_samples[i] = buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) % buffer_size;  // 回绕处理
    }
    
    available_ -= frame_total_samples;  // 更新可用样本计数
    actual_samples = frame_size_;       // 返回每声道样本数
    return true;
}

int AudioRingBuffer::available_samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_ / channels_;
}

bool AudioRingBuffer::has_complete_frame() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_ >= frame_size_ * channels_;
}

void AudioRingBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    write_pos_ = 0;
    read_pos_ = 0;
    available_ = 0;
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
}

AudioProcessor::AudioProcessor() 
    : filter_graph_(nullptr)
    , buffer_src_ctx_(nullptr)
    , buffer_sink_ctx_(nullptr)
    , filter_frame_(nullptr)
    , input_sample_rate_(0)
    , input_channels_(0)
    , input_format_(AV_SAMPLE_FMT_NONE)
    , speed_processing_enabled_(false)
    , last_input_pts_(AV_NOPTS_VALUE)
    , processed_samples_count_(0)
    , first_input_pts_(AV_NOPTS_VALUE) {
}

AudioProcessor::~AudioProcessor() {
    cleanup();
}

bool AudioProcessor::initialize(const AudioProcessParams& params, 
                               int input_sample_rate, 
                               int input_channels,
                               AVSampleFormat input_format) {
    params_ = params;
    input_sample_rate_ = input_sample_rate;
    input_channels_ = input_channels;
    input_format_ = input_format;
    
    // 分配滤波器帧
    filter_frame_ = av_frame_alloc();
    if (!filter_frame_) {
        std::cerr << "无法分配音频滤波器帧" << std::endl;
        return false;
    }
    
    // 无论速度因子是多少，都需要初始化SoundTouch和环形缓冲区
    // 这是为了确保输出帧大小为1536样本，满足AC3编码器要求
    if (params_.enable_speed_change) {
        if (!initialize_speed_processing()) {
            std::cerr << "音频变速处理初始化失败" << std::endl;
            return false;
        }
        speed_processing_enabled_ = true;
        std::cout << "音频变速处理器初始化成功，速度倍数: " << params_.speed_factor << std::endl;
    }
    
    return setup_filter_graph();
}

bool AudioProcessor::setup_filter_graph() {
    int ret;
    
    // 创建滤波器图
    filter_graph_ = avfilter_graph_alloc();
    if (!filter_graph_) {
        std::cerr << "无法分配音频滤波器图" << std::endl;
        return false;
    }
    
    // 查找buffer源滤波器
    const AVFilter* buffer_src = avfilter_get_by_name("abuffer");
    if (!buffer_src) {
        std::cerr << "无法找到abuffer滤波器" << std::endl;
        return false;
    }
    
    // 查找buffer sink滤波器
    const AVFilter* buffer_sink = avfilter_get_by_name("abuffersink");
    if (!buffer_sink) {
        std::cerr << "无法找到abuffersink滤波器" << std::endl;
        return false;
    }
    
    // 设置buffer源参数
    AVChannelLayout channel_layout;
    av_channel_layout_default(&channel_layout, input_channels_);
    char ch_layout_str[64];
    av_channel_layout_describe(&channel_layout, ch_layout_str, sizeof(ch_layout_str));
    
    std::ostringstream args;
    args << "time_base=1/" << input_sample_rate_ 
         << ":sample_rate=" << input_sample_rate_
         << ":sample_fmt=" << av_get_sample_fmt_name(input_format_)
         << ":channel_layout=" << ch_layout_str;
    
    // 创建buffer源上下文
    ret = avfilter_graph_create_filter(&buffer_src_ctx_, buffer_src, "in",
                                      args.str().c_str(), nullptr, filter_graph_);
    if (ret < 0) {
        std::cerr << "无法创建音频buffer源滤波器" << std::endl;
        return false;
    }
    
    // 创建buffer sink上下文
    ret = avfilter_graph_create_filter(&buffer_sink_ctx_, buffer_sink, "out",
                                      nullptr, nullptr, filter_graph_);
    if (ret < 0) {
        std::cerr << "无法创建音频buffer sink滤波器" << std::endl;
        return false;
    }
    
    // 构建滤波器描述字符串
    std::string filter_desc = build_filter_description();
    
    // 解析滤波器描述
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffer_src_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffer_sink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    
    ret = avfilter_graph_parse_ptr(filter_graph_, filter_desc.c_str(),
                                  &inputs, &outputs, nullptr);
    if (ret < 0) {
        std::cerr << "无法解析音频滤波器图: " << filter_desc << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return false;
    }
    
    // 配置滤波器图
    ret = avfilter_graph_config(filter_graph_, nullptr);
    if (ret < 0) {
        std::cerr << "无法配置音频滤波器图" << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return false;
    }
    
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    
    std::cout << "音频处理器初始化成功，滤波器: " << filter_desc << std::endl;
    return true;
}

std::string AudioProcessor::build_filter_description() {
    std::ostringstream desc;
    
    // 开始构建滤波器链
    desc << "[in]";
    
    bool has_filter = false;
    
    // 音量调整
    if (params_.volume_gain != 1.0) {
        if (has_filter) desc << ",";
        desc << "volume=" << params_.volume_gain;
        has_filter = true;
    }
    
    // 低通滤波器
    if (params_.enable_lowpass) {
        if (has_filter) desc << ",";
        desc << "lowpass=f=" << params_.lowpass_frequency;
        has_filter = true;
    }
    
    // 高通滤波器
    if (params_.enable_highpass) {
        if (has_filter) desc << ",";
        desc << "highpass=f=" << params_.highpass_frequency;
        has_filter = true;
    }
    
    // 动态范围压缩
    if (params_.enable_compressor) {
        if (has_filter) desc << ",";
        desc << "acompressor=threshold=" << params_.threshold << "dB"
             << ":ratio=" << params_.ratio
             << ":attack=" << params_.attack
             << ":release=" << params_.release;
        has_filter = true;
    }
    
    // 重采样
    if (params_.enable_resample) {
        if (has_filter) desc << ",";
        desc << "aresample=" << params_.target_sample_rate;
        if (params_.target_channels != input_channels_) {
            desc << ",aformat=channel_layouts=";
            if (params_.target_channels == 1) {
                desc << "mono";
            } else if (params_.target_channels == 2) {
                desc << "stereo";
            } else {
                desc << params_.target_channels << "c";
            }
        }
        has_filter = true;
    }
    
    // 如果没有任何滤波器，添加一个通过滤波器
    if (!has_filter) {
        desc << "anull";
    }
    
    desc << "[out]";
    
    return desc.str();
}

bool AudioProcessor::initialize_speed_processing() {
    try {
        // 初始化SoundTouch
        sound_touch_ = std::make_unique<SoundTouch>();
        sound_touch_->setSampleRate(input_sample_rate_);
        sound_touch_->setChannels(input_channels_);
        
        // 设置速度，但不改变音调
        sound_touch_->setTempo(params_.speed_factor);
        sound_touch_->setPitch(1.0);
        
        // 初始化环形缓冲区，frame_size=1536适配AC3编码器
        ring_buffer_ = std::make_unique<AudioRingBuffer>(1536, input_channels_, input_sample_rate_);
        
        // 初始化临时缓冲区
        temp_buffer_.resize(8192 * input_channels_);
        
        std::cout << "音频变速处理器初始化成功，速度倍数: " << params_.speed_factor << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SoundTouch初始化失败: " << e.what() << std::endl;
        return false;
    }
}

int64_t AudioProcessor::calculate_new_pts(int64_t original_pts) const {
    // 新的实现：完全基于线性时间轴，不依赖原始PTS
    // 音频PTS已经是基于采样数的线性时间轴，直接返回即可
    // 变速调整在生成线性时间轴时已经通过SoundTouch自然实现
    return original_pts;
}

AVFrame* AudioProcessor::create_output_frame(const float* samples, int num_samples, int64_t pts) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }
    
    frame->nb_samples = num_samples;
    frame->format = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&frame->ch_layout, input_channels_);
    frame->sample_rate = input_sample_rate_;
    frame->pts = pts;
    
    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }
    
    // 转换平面格式
    if (input_channels_ == 1) {
        float* output = reinterpret_cast<float*>(frame->data[0]);
        memcpy(output, samples, num_samples * sizeof(float));
    } else if (input_channels_ == 2) {
        float* left = reinterpret_cast<float*>(frame->data[0]);
        float* right = reinterpret_cast<float*>(frame->data[1]);
        
        for (int i = 0; i < num_samples; ++i) {
            left[i] = samples[i * 2];
            right[i] = samples[i * 2 + 1];
        }
    }
    
    return frame;
}

bool AudioProcessor::process_samples_through_soundtouch(const float* input_samples, int num_samples, AudioFrameQueue* output_queue) {
    // 输入样本到SoundTouch
    sound_touch_->putSamples(input_samples, num_samples);
    
    // 从SoundTouch获取处理后的样本
    int received_samples;
    while ((received_samples = sound_touch_->receiveSamples(temp_buffer_.data(), temp_buffer_.size() / input_channels_)) > 0) {
        // 将样本写入环形缓冲区
        if (!ring_buffer_->write_samples(temp_buffer_.data(), received_samples)) {
            std::cerr << "环形缓冲区写入失败" << std::endl;
            continue;
        }
        
        // 从环形缓冲区读取固定大小的帧
        std::vector<float> frame_buffer(1536 * input_channels_);
        int actual_samples;
        
        while (ring_buffer_->read_frame(frame_buffer.data(), actual_samples)) {
            // 为输出帧生成基于采样数的线性时间戳，完全不依赖输入帧PTS
            // 音频的PTS单位就是"采样数"，从0开始连续递增
            int64_t output_pts = processed_samples_count_;
            
            // 创建输出帧
            AVFrame* output_frame = create_output_frame(frame_buffer.data(), actual_samples, output_pts);
            if (output_frame) {
                output_queue->push(output_frame);
                // 递增已处理的样本数计数器，为下一帧准备
                processed_samples_count_ += actual_samples;
            }
        }
    }
    
    return true;
}

bool AudioProcessor::process_samples_through_soundtouch_with_frame_pts(const float* input_samples, int num_samples, int64_t input_pts, AudioFrameQueue* output_queue) {
    // 输入样本到SoundTouch
    sound_touch_->putSamples(input_samples, num_samples);
    
    // 从SoundTouch获取处理后的样本
    int received_samples;
    while ((received_samples = sound_touch_->receiveSamples(temp_buffer_.data(), temp_buffer_.size() / input_channels_)) > 0) {
        // 将样本写入环形缓冲区
        if (!ring_buffer_->write_samples(temp_buffer_.data(), received_samples)) {
            std::cerr << "环形缓冲区写入失败" << std::endl;
            continue;
        }
        
        // 从环形缓冲区读取固定大小的帧
        std::vector<float> frame_buffer(1536 * input_channels_);
        int actual_samples;
        
        while (ring_buffer_->read_frame(frame_buffer.data(), actual_samples)) {
            // 为输出帧生成基于采样数的线性时间戳，完全不依赖输入帧PTS
            // 音频的PTS单位就是"采样数"，从0开始连续递增
            int64_t output_pts = processed_samples_count_;
            
            // 创建输出帧
            AVFrame* output_frame = create_output_frame(frame_buffer.data(), actual_samples, output_pts);
            if (output_frame) {
                output_queue->push(output_frame);
                // 递增已处理的样本数计数器，为下一帧准备
                processed_samples_count_ += actual_samples;
            }
        }
    }
    
    return true;
}

bool AudioProcessor::process_frame_with_speed(AVFrame* input_frame, AudioFrameQueue* output_queue) {
    // 更新时间戳信息
    if (input_frame->pts != AV_NOPTS_VALUE) {
        last_input_pts_ = input_frame->pts;
        // 记录第一个有效时间戳作为基准
        if (first_input_pts_ == AV_NOPTS_VALUE) {
            first_input_pts_ = input_frame->pts;
        }
    }
    
    // 转换为float格式进行处理
    int num_samples = input_frame->nb_samples;
    std::vector<float> float_samples(num_samples * input_channels_);
    
    // 根据输入格式转换
    if (input_format_ == AV_SAMPLE_FMT_FLTP) {
        // 平面浮点格式
        if (input_channels_ == 1) {
            float* src = reinterpret_cast<float*>(input_frame->data[0]);
            memcpy(float_samples.data(), src, num_samples * sizeof(float));
        } else if (input_channels_ == 2) {
            float* left = reinterpret_cast<float*>(input_frame->data[0]);
            float* right = reinterpret_cast<float*>(input_frame->data[1]);
            
            for (int i = 0; i < num_samples; ++i) {
                float_samples[i * 2] = left[i];
                float_samples[i * 2 + 1] = right[i];
            }
        }
    } else {
        // 其他格式需要转换（这里简化处理）
        std::cerr << "不支持的音频格式，需要先转换为FLTP" << std::endl;
        return false;
    }
    
    // 通过SoundTouch处理，使用统一的时间戳计算
    return process_samples_through_soundtouch_with_frame_pts(float_samples.data(), num_samples, input_frame->pts, output_queue);
}

bool AudioProcessor::process_frame(AVFrame* input_frame, AudioFrameQueue* output_queue) {
    if (!filter_graph_ || !buffer_src_ctx_ || !buffer_sink_ctx_) {
        std::cerr << "音频处理器未初始化" << std::endl;
        return false;
    }
    
    // 如果启用了变速处理，直接进行变速处理
    if (speed_processing_enabled_) {
        return process_frame_with_speed(input_frame, output_queue);
    }
    
    // 原有的滤波器处理流程
    // 将帧送入滤波器图
    int ret = av_buffersrc_add_frame_flags(buffer_src_ctx_, input_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        std::cerr << "向音频滤波器发送帧失败" << std::endl;
        return false;
    }
    
    // 从滤波器图获取处理后的帧
    while (true) {
        ret = av_buffersink_get_frame(buffer_sink_ctx_, filter_frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            std::cerr << "从音频滤波器获取帧失败" << std::endl;
            return false;
        }
        
        // 复制帧并添加到输出队列
        AVFrame* output_frame = av_frame_alloc();
        if (av_frame_ref(output_frame, filter_frame_) < 0) {
            std::cerr << "无法复制音频处理后的帧" << std::endl;
            av_frame_free(&output_frame);
            av_frame_unref(filter_frame_);
            continue;
        }
        
        output_queue->push(output_frame);
        av_frame_unref(filter_frame_);
    }
    
    return true;
}

bool AudioProcessor::flush(AudioFrameQueue* output_queue) {
    if (!filter_graph_ || !buffer_src_ctx_ || !buffer_sink_ctx_) {
        return false;
    }
    
    // 如果启用了变速处理，需要刷新SoundTouch和环形缓冲区
    if (speed_processing_enabled_ && sound_touch_) {
        // 刷新SoundTouch中剩余的样本
        sound_touch_->flush();
        
        int received_samples;
        while ((received_samples = sound_touch_->receiveSamples(temp_buffer_.data(), temp_buffer_.size() / input_channels_)) > 0) {
            // 将样本写入环形缓冲区
            ring_buffer_->write_samples(temp_buffer_.data(), received_samples);
            
            // 从环形缓冲区读取固定大小的帧
            std::vector<float> frame_buffer(1536 * input_channels_);
            int actual_samples;
            
            while (ring_buffer_->read_frame(frame_buffer.data(), actual_samples)) {
                // 为输出帧生成基于采样数的线性时间戳，完全不依赖输入帧PTS
                // 音频的PTS单位就是"采样数"，从0开始连续递增
                int64_t output_pts = processed_samples_count_;
                
                AVFrame* output_frame = create_output_frame(frame_buffer.data(), actual_samples, output_pts);
                if (output_frame) {
                    output_queue->push(output_frame);
                    // 递增已处理的样本数计数器，为下一帧准备
                    processed_samples_count_ += actual_samples;
                }
            }
        }
        
        // 处理环形缓冲区中剩余的不完整帧
        int remaining_samples = ring_buffer_->available_samples();
        if (remaining_samples > 0 && remaining_samples < 1536) {
            // 对于不足1536样本的剩余数据，填充到1536再输出
            std::vector<float> padded_buffer(1536 * input_channels_, 0.0f);
            
            // 从环形缓冲区读取所有剩余样本
            const auto& buffer = ring_buffer_->get_buffer();
            int buffer_size = static_cast<int>(buffer.size());
            int read_pos = ring_buffer_->get_read_pos();
            
            for (int i = 0; i < remaining_samples * input_channels_; ++i) {
                padded_buffer[i] = buffer[(read_pos + i) % buffer_size];
            }
            
            // 清空环形缓冲区
            ring_buffer_->clear();
            
            // 为输出帧生成基于采样数的线性时间戳，完全不依赖输入帧PTS
            // 音频的PTS单位就是"采样数"，从0开始连续递增
            int64_t output_pts = processed_samples_count_;
            
            AVFrame* output_frame = create_output_frame(padded_buffer.data(), 1536, output_pts);
            if (output_frame) {
                output_queue->push(output_frame);
                // 递增已处理的样本数计数器
                processed_samples_count_ += 1536;
            }
        }
        
        return true;
    }
    
    // 原有的滤波器刷新流程
    // 发送EOF到滤波器图
    int ret = av_buffersrc_add_frame_flags(buffer_src_ctx_, nullptr, 0);
    if (ret < 0) {
        std::cerr << "音频滤波器刷新失败" << std::endl;
        return false;
    }
    
    // 获取剩余的帧
    while (true) {
        ret = av_buffersink_get_frame(buffer_sink_ctx_, filter_frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            std::cerr << "从音频滤波器获取刷新帧失败" << std::endl;
            return false;
        }
        
        AVFrame* output_frame = av_frame_alloc();
        if (av_frame_ref(output_frame, filter_frame_) < 0) {
            std::cerr << "无法复制音频刷新帧" << std::endl;
            av_frame_free(&output_frame);
            av_frame_unref(filter_frame_);
            continue;
        }
        
        output_queue->push(output_frame);
        av_frame_unref(filter_frame_);
    }
    
    return true;
}

void AudioProcessor::cleanup() {
    if (filter_frame_) {
        av_frame_free(&filter_frame_);
    }
    
    if (filter_graph_) {
        avfilter_graph_free(&filter_graph_);
    }
    
    // 清理变速处理相关资源
    sound_touch_.reset();
    ring_buffer_.reset();
    temp_buffer_.clear();
    
    speed_processing_enabled_ = false;
    last_input_pts_ = AV_NOPTS_VALUE;
    processed_samples_count_ = 0;
}

// 音频处理线程函数
void audio_process_thread_func(AudioFrameQueue* input_frame_queue,
                              AudioFrameQueue* output_frame_queue, 
                              const AudioProcessParams& params,
                              int sample_rate, int channels, 
                              AVSampleFormat format) {
    std::cout << "音频处理线程已启动" << std::endl;
    
    AudioProcessor processor;
    if (!processor.initialize(params, sample_rate, channels, format)) {
        std::cerr << "音频处理器初始化失败" << std::endl;
        return;
    }
    
    int frame_count = 0;
    AVFrame* frame = nullptr;
    
    // 主处理循环
    while (input_frame_queue->pop(frame)) {
        if (!frame) {
            break;
        }
        
        if (!processor.process_frame(frame, output_frame_queue)) {
            std::cerr << "音频帧处理失败" << std::endl;
        }
        
        av_frame_free(&frame);
        frame_count++;
    }
    
    // 刷新处理器
    std::cout << "刷新音频处理器..." << std::endl;
    processor.flush(output_frame_queue);
    
    // 标记输出队列结束
    output_frame_queue->finish();
    
    std::cout << "音频处理线程结束，处理了 " << frame_count << " 帧" << std::endl;
}

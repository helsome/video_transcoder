/**
 * =====================================================================================
 * 音频解码器模块 (audio_decoder.cpp) - 高效音频解码与格式处理引擎
 * =====================================================================================
 * 
 * [整体概述]
 * 这是音频处理流水线的"入口大门"，负责将压缩的音频数据包解码为原始PCM音频帧。
 * 在项目架构中，它属于数据转换层——类似于工厂流水线的原材料处理车间，
 * 将"压缩原料"转换为"可加工的半成品"。
 * 
 * 架构定位：
 * - 数据解码层：压缩音频→原始PCM的转换
 * - 格式适配层：处理多种音频格式(AAC, AC3, MP3等)
 * - 流控制层：管理音频数据流的连续性和完整性
 * 
 * 交互关系：
 * - 上游：接收解封装线程的压缩音频包
 * - 下游：输出PCM帧给音频处理线程
 * - 核心依赖：FFmpeg解码器框架
 * - 数据流：AVPacket → AVFrame 的核心转换
 * 
 * [技术重点解析]
 * 
 * **核心算法：FFmpeg异步解码API**
 * 现代FFmpeg解码流程：
 * 1. avcodec_send_packet()：发送压缩包到解码器
 * 2. avcodec_receive_frame()：从解码器接收解码帧
 * 3. 异步处理：解码器内部可能缓存多个包才输出一帧
 * 4. 刷新处理：发送nullptr包刷出解码器残留数据
 * 
 * **关键数据处理：Planar vs Packed音频格式**
 * - Planar(平面)格式：各声道数据分离存储 [LLLL][RRRR]
 * - Packed(交错)格式：各声道数据交错存储 [LRLRLR]
 * - 输出统一：转换为交错格式便于后续处理
 * 
 * **设计模式应用：**
 * 1. 状态机模式：解码器的多种状态管理(初始化、解码、刷新、结束)
 * 2. 适配器模式：统一不同音频格式的处理接口
 * 3. 生产者模式：向音频帧队列持续产生数据
 * 4. 资源管理模式：RAII管理FFmpeg解码器资源
 * 
 * ["答辩"视角下的潜在问题]
 * 
 * **设计抉择分析：**
 * Q: 为什么使用异步解码API而不是同步API？
 * A: 
 * - 当前方案：avcodec_send_packet() + avcodec_receive_frame()
 *   优点：符合现代FFmpeg设计、支持硬件加速、内存管理更好
 *   缺点：逻辑稍复杂、需要处理EAGAIN状态
 * 
 * - 替代方案：avcodec_decode_audio4() (已废弃)
 *   优点：逻辑简单、一次调用完成
 *   缺点：不支持硬件加速、内存拷贝多、已被标记废弃
 * 
 * **潜在风险点：**
 * 1. 内存泄漏：AVFrame/AVPacket的C风格内存管理
 *    - 风险场景：异常退出时未调用av_frame_free()
 *    - 缓解策略：RAII包装、智能指针封装
 * 
 * 2. 音频格式兼容性：不同编码器的格式差异
 *    - 风险场景：某些特殊采样率或声道配置不支持
 *    - 缓解策略：格式检测、自动重采样
 * 
 * 3. 线程安全性：FFmpeg解码器的并发访问
 *    - 风险场景：多线程同时访问同一解码器实例
 *    - 缓解策略：每线程独立解码器实例
 */

#include "audio_decoder.h"
#include <iostream>
#include <fstream>

/**
 * PCM音频帧保存函数：调试和验证工具
 * 
 * @param frame: 解码后的音频帧
 * @param filename: 输出PCM文件名
 * 
 * 功能说明：
 * - 将AVFrame中的音频数据保存为原始PCM文件
 * - 自动处理Planar和Packed两种音频格式
 * - 支持多声道音频的正确交错输出
 * 
 * 技术要点：
 * - 格式检测：使用av_sample_fmt_is_planar()判断数据布局
 * - 交错转换：将平面格式转换为交错格式统一输出
 * - 字节序处理：保持原始字节序，便于外部工具播放
 */
// save_pcm_frame 函数保持不变
void save_pcm_frame(AVFrame* frame, const char* filename) {
    std::ofstream file(filename, std::ios::app | std::ios::binary);
    if (!file) {
        std::cerr << "无法打开PCM输出文件。" << std::endl;
        return;
    }
    
    int data_size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
    if (data_size < 0) {
        std::cerr << "计算数据大小失败" << std::endl;
        return;
    }
    
    /**
     * 音频格式分支处理：统一不同存储布局
     * 
     * Planar格式特点：
     * - 数据按声道分离：frame->data[0]=左声道, frame->data[1]=右声道
     * - 内存布局：[L1,L2,L3...][R1,R2,R3...]
     * - 典型格式：FLTP, S16P, S32P
     * 
     * Packed格式特点：
     * - 数据按样本交错：[L1,R1,L2,R2,L3,R3...]
     * - 内存布局：连续存储在frame->data[0]中
     * - 典型格式：FLT, S16, S32
     */
    // 检查是否为平面（planar）格式
    if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format))) {
        /**
         * 平面格式处理：手动交错输出
         * 算法：按样本逐个交错各声道数据
         * 输出格式：LRLRLR... (立体声情况)
         * 性能考虑：逐字节处理，适合调试，生产环境可考虑SIMD优化
         */
        // 平面格式，例如 FLTP, S16P, S32P
        // 数据按通道分离存储，需要交错写入文件
        for (int i = 0; i < frame->nb_samples; i++) {
            for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
                file.write(reinterpret_cast<const char*>(frame->data[ch] + data_size * i), data_size);
            }
        }
    } else {
        /**
         * 交错格式处理：直接批量输出
         * 优势：高效的内存拷贝，无需格式转换
         * 适用：已经是目标格式的音频数据
         */
        // 交错格式（packed），例如 FLT, S16, S32
        // 所有通道数据已连续存放，直接写入即可
        file.write(reinterpret_cast<const char*>(frame->data[0]), data_size * frame->nb_samples * frame->ch_layout.nb_channels);
    }
    
    file.close();
}


// audio_decode_thread_func 已更新为保存完整音频流
void audio_decode_thread_func(AudioPacketQueue* audio_packet_queue,
                             AVCodecParameters* codec_params,
                             const char* output_filename) {
    std::cout << "音频解码线程已启动，将保存完整音频流。" << std::endl;
    
    // --- 解码器初始化部分保持不变 ---
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "未找到音频解码器，ID: " << codec_params->codec_id << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "无法分配音频解码器上下文。" << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    if (avcodec_parameters_to_context(codec_context, codec_params) < 0) {
        std::cerr << "无法将解码参数复制到音频解码器上下文。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "无法打开音频解码器。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
         std::cerr << "无法分配音频帧。" << std::endl;
         avcodec_free_context(&codec_context);
         avcodec_parameters_free(&codec_params);
         return;
    }

    // 在开始解码前，清空输出文件
    std::ofstream ofs(output_filename, std::ios::trunc);
    ofs.close();

    // --- 修改解码循环以处理所有帧 ---
    bool done = false;
    while (!done) {
        // 从队列中获取数据包，nullptr表示码流结束，需要刷出解码器
        AVPacket* packet = nullptr;
        if (!audio_packet_queue->pop(packet)) {
            packet = nullptr; // 队列结束
        }

        // 将数据包（或nullptr用于刷出）发送给解码器
        int ret = avcodec_send_packet(codec_context, packet);
        if (ret < 0) {
            std::cerr << "向音频解码器发送 AVPacket 时出错" << std::endl;
            done = true; // 发送失败，结束循环
        }

        // packet的所有权已转移给解码器，可以释放它
        // 注意：即使packet是nullptr，av_packet_free也不会出错
        if (packet) {
            av_packet_free(&packet);
        }

        // 循环从解码器接收所有可用的音频帧
        while (ret >= 0) {
            // 从解码器接收一个音频帧
            int receive_ret = avcodec_receive_frame(codec_context, frame);
            
            if (receive_ret == AVERROR(EAGAIN)) {
                // 解码器需要更多的数据包才能输出一个帧，跳出内层循环去获取下一个packet
                break;
            } else if (receive_ret == AVERROR_EOF) {
                // 解码器已被完全刷出，没有更多帧可以输出，结束所有循环
                done = true;
                break;
            } else if (receive_ret < 0) {
                // 发生了真正的解码错误
                std::cerr << "从音频解码器接收 AVFrame 时出错" << std::endl;
                done = true;
                break;
            }
            
            // 成功接收一个帧，保存它
            save_pcm_frame(frame, output_filename);
        }
    }
    
    // --- 清理资源部分保持不变 ---
    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
    avcodec_parameters_free(&codec_params);
    
    std::cout << "音频解码线程已结束。" << std::endl;
}

/**
 * [代码逻辑详述]
 * 
 * 音频解码到队列函数：生产者线程的核心实现
 * 
 * 执行流程：
 * 第一步：解码器初始化与配置
 * 第二步：循环从包队列获取压缩数据
 * 第三步：发送包到解码器进行异步解码
 * 第四步：循环接收解码后的音频帧
 * 第五步：帧复制并推送到输出队列
 * 第六步：解码器刷新与资源清理
 * 
 * @param audio_packet_queue: 输入的压缩音频包队列
 * @param audio_frame_queue: 输出的PCM音频帧队列  
 * @param codec_params: 音频编解码器参数
 */
// 新的解码到Frame队列函数（用于完整转码流程）
void audio_decode_to_frames_thread_func(AudioPacketQueue* audio_packet_queue,
                                        AudioFrameQueue* audio_frame_queue,
                                        AVCodecParameters* codec_params) {
    std::cout << "音频解码线程（输出到Frame队列）已启动。" << std::endl;
    
    /**
     * 第一步：解码器查找与初始化
     * 
     * 解码器查找：根据codec_id查找对应的解码器实现
     * 支持格式：AAC, AC3, MP3, FLAC等主流音频格式
     * 硬件加速：某些平台支持硬件音频解码(较少见)
     */
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "未找到音频解码器，ID: " << codec_params->codec_id << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    /**
     * 解码器上下文分配：为解码器创建工作环境
     * 上下文作用：存储解码器状态、缓冲区、配置参数等
     * 内存管理：使用FFmpeg的内存管理函数，确保正确释放
     */
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "无法分配音频解码器上下文。" << std::endl;
        avcodec_parameters_free(&codec_params);
        return;
    }

    /**
     * 参数复制：将容器级参数复制到解码器上下文
     * 包含信息：采样率、声道数、样本格式、比特率等
     * 重要性：解码器需要这些信息正确解析压缩数据
     */
    if (avcodec_parameters_to_context(codec_context, codec_params) < 0) {
        std::cerr << "无法将解码参数复制到音频解码器上下文。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    /**
     * 解码器打开：启动解码器并分配内部资源
     * 可能失败的原因：不支持的参数组合、硬件资源不足等
     * 性能影响：某些解码器可能需要较长的初始化时间
     */
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "无法打开音频解码器。" << std::endl;
        avcodec_free_context(&codec_context);
        avcodec_parameters_free(&codec_params);
        return;
    }

    /**
     * 输出帧分配：用于接收解码结果的容器
     * AVFrame结构：包含音频数据指针、格式信息、时间戳等
     * 重用策略：单个AVFrame重复使用，减少分配开销
     */
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
         std::cerr << "无法分配音频帧。" << std::endl;
         avcodec_free_context(&codec_context);
         avcodec_parameters_free(&codec_params);
         return;
    }

    int frame_count = 0;
    bool done = false;
    
    /**
     * 第二步：主解码循环
     * 
     * 循环逻辑：
     * - 从输入队列获取压缩包
     * - 发送包到解码器
     * - 接收所有可用的解码帧
     * - 复制帧并推送到输出队列
     * - 处理队列结束信号
     */
    while (!done) {
        /**
         * 包获取：从线程安全队列中取出音频包
         * 阻塞特性：队列为空时线程会休眠等待
         * 结束信号：nullptr表示上游数据流结束
         */
        AVPacket* packet = nullptr;
        if (!audio_packet_queue->pop(packet)) {
            packet = nullptr; // 队列结束
        }

        /**
         * 包发送：将压缩包发送给解码器
         * 异步特性：解码器可能缓存多个包才输出帧
         * 刷新模式：发送nullptr包触发解码器刷新
         */
        int ret = avcodec_send_packet(codec_context, packet);
        if (ret < 0) {
            std::cerr << "向音频解码器发送 AVPacket 时出错" << std::endl;
            done = true;
        }

        /**
         * 包内存管理：FFmpeg的包所有权转移机制
         * 重要：send_packet后包的所有权转移给解码器
         * 安全：即使是nullptr包，av_packet_free也不会出错
         */
        if (packet) {
            av_packet_free(&packet);
        }

        /**
         * 第三步：帧接收循环
         * 
         * 内层循环：从解码器接收所有可用帧
         * 重要：一个包可能产生多个帧，或多个包产生一个帧
         */
        while (ret >= 0) {
            /**
             * 帧接收：从解码器获取解码后的音频帧
             * 返回值含义：
             * - 0: 成功获取一帧
             * - AVERROR(EAGAIN): 需要更多输入包
             * - AVERROR_EOF: 解码器已刷新完毕
             * - 其他负值: 解码错误
             */
            int receive_ret = avcodec_receive_frame(codec_context, frame);
            
            if (receive_ret == AVERROR(EAGAIN)) {
                break; // 需要更多数据包，跳出内层循环
            } else if (receive_ret == AVERROR_EOF) {
                done = true; // 解码器已完全刷新，结束所有循环
                break;
            } else if (receive_ret < 0) {
                std::cerr << "从音频解码器接收 AVFrame 时出错" << std::endl;
                done = true;
                break;
            }
            
            /**
             * 第四步：帧复制与队列推送
             * 
             * 帧复制：创建新的AVFrame并复制数据
             * 必要性：原frame会被重用，必须复制保存
             * 内存管理：使用av_frame_ref进行引用计数管理
             */
            AVFrame* output_frame = av_frame_alloc();
            if (av_frame_ref(output_frame, frame) < 0) {
                std::cerr << "无法复制音频帧。" << std::endl;
                av_frame_free(&output_frame);
                continue;
            }
            
            /**
             * 队列推送：将复制的帧发送给下游处理线程
             * 线程安全：队列内部处理并发访问保护
             * 内存转移：帧的所有权转移给队列和下游线程
             */
            audio_frame_queue->push(output_frame);
            frame_count++;
        }
    }
    
    /**
     * 第五步：队列结束标记与资源清理
     * 
     * 队列完成信号：通知下游线程没有更多数据
     * 重要性：没有此调用，下游线程会永久阻塞等待
     * 线程协调：实现优雅的多线程关闭序列
     */
    // 标记音频帧队列结束
    audio_frame_queue->finish();

    /**
     * 资源释放：RAII原则的体现
     * 释放顺序：先释放使用资源，再释放基础资源
     * 内存安全：确保没有悬挂指针和内存泄漏
     */
    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
    avcodec_parameters_free(&codec_params);
    
    std::cout << "音频解码线程（输出到Frame队列）已结束。共解码 " << frame_count << " 帧。" << std::endl;
}

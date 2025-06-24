/**
 * =====================================================================================
 * 高性能音视频转码器 - 主控制器模块 (Transcoder.cpp)
 * =====================================================================================
 * 
 * [整体概述]
 * 这是整个音视频转码系统的"总指挥官"，负责协调6个核心线程的工作流程。
 * 在项目架构中，它处于最高层——系统入口层，类似于微服务架构中的API Gateway，
 * 但这里是多线程任务的调度中心。
 * 
 * 架构定位：
 * - 系统入口层：解析命令行参数，验证输入
 * - 线程协调层：创建并管理6个工作线程的生命周期
 * - 资源管理层：统一管理FFmpeg资源的分配与释放
 * 
 * 交互模块：
 * - 与所有处理模块交互：demuxer, decoder, processor, encoder, muxer
 * - 通过线程安全队列与各模块通信
 * - 依赖GLFW进行OpenGL上下文管理
 * 
 * [技术重点解析]
 * 
 * **核心架构模式：生产者-消费者 + 流水线并行**
 * 采用了经典的"生产者-消费者"模式的变种——多级流水线：
 * 输入文件 → [解封装] → [解码] → [处理] → [编码] → [封装] → 输出文件
 *    1线程     2线程      2线程     1线程     1线程
 * 
 * **关键数据结构：ThreadSafeQueue<T>**
 * 使用模板化的线程安全队列作为线程间通信的"神经网络"：
 * - 基于std::queue + std::mutex + std::condition_variable
 * - 实现阻塞式生产者-消费者通信
 * - 避免了忙等待，提高CPU效率
 * 
 * **设计模式应用：**
 * 1. 工厂模式：音频编码器工厂(TargetAudioFormat)支持多种格式
 * 2. 参数对象模式：各种Params结构体封装复杂参数传递
 * 3. RAII模式：AVCodecParameters自动释放管理
 * 
 * **并发架构设计：6线程分工明确**
 * - 解封装线程：I/O密集型，独立线程避免阻塞
 * - 解码线程(2个)：CPU密集型，音视频分离提高并行度  
 * - 处理线程(2个)：GPU+CPU混合，音频(SoundTouch)视频(OpenGL)
 * - 编码线程(2个)：CPU密集型，利用多核编码能力
 * - 封装线程：I/O密集型，负责最终文件写入
 * 
 * ["答辩"视角下的潜在问题]
 * 
 * **设计抉择分析：**
 * Q: 为什么选择6线程而不是更多？
 * A: 
 * - 当前方案：解封装(1) + 解码(2) + 处理(2) + 编码(2) + 封装(1) = 8线程，但处理和编码共享
 * - 替代方案1：单线程顺序处理 - 优点：简单无锁，缺点：性能差10倍
 * - 替代方案2：更多线程(如12个) - 优点：理论并行度更高，缺点：线程调度开销、内存争用严重
 * 
 * 选择6线程是基于：
 * 1. 音视频处理的天然分离性
 * 2. I/O与计算密集任务的分离
 * 3. 现代CPU核心数(4-8核)的匹配
 * 
 * **潜在风险点：**
 * 1. 内存压力：多个队列同时缓存大量AVFrame，高分辨率视频可能OOM
 *    - 风险场景：4K视频，队列积压50帧 = 50*4096*2160*3*4字节 ≈ 2.5GB
 *    - 缓解策略：队列大小限制、内存监控
 * 
 * 2. 队列死锁风险：如果某个线程异常退出，其他线程可能永久阻塞
 *    - 风险场景：视频解码失败但未通知后续线程
 *    - 缓解策略：超时机制、异常传播机制
 * 
 * 3. 音画同步丢失：变速处理时时间戳计算错误
 *    - 风险场景：高倍速(>3x)或低倍速(<0.5x)时累积误差
 *    - 缓解策略：独立时间戳重生成、PTS校验
 */

#include <iostream>
#include <thread>
#include <vector>
#include "demuxer.h"
#include "video_decoder.h"
#include "audio_decoder.h"
#include "video_processor.h"
#include "audio_processor.h"
#include "video_encoder.h"
#include "audio_encoder.h"
#include "muxer.h"
#include "queue.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <GLFW/glfw3.h>

/**
 * [代码逻辑详述]
 * 
 * 主函数执行流程：
 * 第一阶段：参数解析与验证 (行25-60)
 * 第二阶段：媒体文件信息获取 (行61-75) 
 * 第三阶段：数据流水线构建 (行76-85)
 * 第四阶段：6线程启动与配置 (行86-190)
 * 第五阶段：线程同步等待 (行191-200)
 * 第六阶段：资源清理 (行201-223)
 */
int main(int argc, char* argv[]) {
    // ==================== 第一阶段：参数解析与健壮性检查 ====================
    
    /**
     * 命令行接口设计：支持9个可选参数，向后兼容
     * 设计考量：参数过多时UX复杂，但提供了最大灵活性
     * 答辩要点：解释为什么不用配置文件而用命令行参数
     */
    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <输入视频文件> <输出视频文件> [变速倍数] [旋转角度] [模糊:0/1] [锐化:0/1] [灰度:0/1] [亮度:0.0-2.0] [对比度:0.0-2.0]" << std::endl;
        std::cerr << "例如: " << argv[0] << " input.mp4 output.avi 1.5 90 0 1 0 1.2 1.3" << std::endl;
        return -1;
    }

    // 核心参数提取
    const char* input_filename = argv[1];
    const char* output_filename = argv[2];
    
    /**
     * 变速倍数解析：支持0.1x到5x倍速
     * 技术细节：double类型保证精度，std::atof提供容错性
     * 风险点：用户输入非数字时std::atof返回0.0，需要范围检查兜底
     */
    double speed_factor = (argc > 3) ? std::atof(argv[3]) : 1.0;
    float rotation_angle = (argc > 4) ? std::atof(argv[4]) : 0.0;
    
    // 滤镜参数：布尔值通过整数0/1表示，提供默认值策略
    bool enable_blur = (argc > 5) ? (std::atoi(argv[5]) != 0) : false;
    bool enable_sharpen = (argc > 6) ? (std::atoi(argv[6]) != 0) : true;  // 默认启用锐化
    bool enable_grayscale = (argc > 7) ? (std::atoi(argv[7]) != 0) : false;
    float brightness = (argc > 8) ? std::atof(argv[8]) : 1.1f;
    float contrast = (argc > 9) ? std::atof(argv[9]) : 1.2f;

    /**
     * 参数边界检查：防御性编程实践
     * 答辩要点：为什么选择这些边界值？
     * - 0.1x-5x：基于人类感知极限和技术可行性
     * - 亮度/对比度0-2倍：超出此范围图像质量严重劣化
     */

    if (speed_factor <= 0.1 || speed_factor > 5.0) {
        std::cerr << "错误: 变速倍数必须在0.1到5.0之间" << std::endl;
        return -1;
    }

    // 参数验证
    if (brightness < 0.0f || brightness > 2.0f) {
        std::cerr << "错误: 亮度值必须在0.0到2.0之间" << std::endl;
        return -1;
    }
    
    if (contrast < 0.0f || contrast > 2.0f) {
        std::cerr << "错误: 对比度值必须在0.0到2.0之间" << std::endl;
        return -1;
    }

    std::cout << "开始增强转码流程（音视频处理）" << std::endl;
    std::cout << "输入文件: " << input_filename << std::endl;
    std::cout << "输出文件: " << output_filename << std::endl;
    std::cout << "变速倍数: " << speed_factor << "x" << std::endl;
    std::cout << "旋转角度: " << rotation_angle << "度" << std::endl;
    std::cout << "滤镜设置: 模糊=" << (enable_blur ? "开" : "关") 
              << " 锐化=" << (enable_sharpen ? "开" : "关")
              << " 灰度=" << (enable_grayscale ? "开" : "关") << std::endl;
    std::cout << "图像调整: 亮度=" << brightness << " 对比度=" << contrast << std::endl;

    // ==================== 第二阶段：媒体文件分析 ====================
    
    /**
     * 流信息获取：FFmpeg探测阶段
     * 技术要点：avformat_find_stream_info()是耗时操作，但对后续处理至关重要
     * 包含内容：编解码器参数、分辨率、帧率、采样率等元数据
     */
    //get_stream_info 是一个函数，接受一个指向输入文件名的常量字符指针 input_filename 和一个 StreamInfo 类型的引用 info 作为参数。
    //该函数返回一个布尔值，用于指示是否成功获取流信息并填充到 info 中。
    StreamInfo stream_info;
    if (!get_stream_info(input_filename, stream_info)) {
        std::cerr << "错误: 无法获取输入文件信息" << std::endl;
        return -1;
    }

    std::cout << "视频信息: " << stream_info.video_width << "x" << stream_info.video_height 
              << " @ " << stream_info.video_fps << "fps" << std::endl;
    std::cout << "音频信息: " << stream_info.audio_sample_rate << "Hz, " 
              << stream_info.audio_channels << " 声道" << std::endl;

    // ==================== 第三阶段：流水线数据队列构建 ====================
    
    /**
     * 8个线程安全队列：系统的"血管网络"
     * 设计模式：类型安全的模板队列，避免void*的类型风险
     * 内存管理：队列持有智能指针，自动释放AVPacket/AVFrame
     */
    VideoPacketQueue raw_video_packets;        // 解封装→视频解码
    AudioPacketQueue raw_audio_packets;        // 解封装→音频解码
    VideoFrameQueue decoded_video_frames;      // 视频解码→视频处理
    AudioFrameQueue decoded_audio_frames;      // 音频解码→音频处理
    VideoFrameQueue processed_video_frames;    // 视频处理→视频编码
    AudioFrameQueue processed_audio_frames;    // 音频处理→音频编码
    EncodedVideoPacketQueue encoded_video_packets;  // 视频编码→封装
    EncodedAudioPacketQueue encoded_audio_packets;  // 音频编码→封装

    /**
     * 线程容器：std::vector<std::thread>管理线程生命周期
     * 选择vector而非原始数组：动态大小、RAII自动析构
     */
    std::vector<std::thread> threads;

    // ==================== 第四阶段：6线程启动序列 ====================

    /**
     * 线程1：解封装线程 (I/O密集型)
     * 职责：读取输入文件，将音视频包分发到不同队列
     * 技术细节：使用av_read_frame()循环读取，根据stream_index分发
     * 性能考量：文件I/O可能成为瓶颈，特别是网络文件
     */
    DemuxerParams demux_params;
    demux_params.input_filename = input_filename;
    demux_params.max_frames = 0;  // 0表示处理整个文件
    //emplace_back 是一个成员函数，用于在 vector 的末尾添加一个新元素，并返回对该元素的引用。放回
    // 参数对象传引用避免拷贝，提升性能
    //此函数接受多个参数，包括一个函数引用、std::reference_wrapper 类型的参数以及指针类型参数，用于以完美转发的方式构造 std::thread 对象。
    threads.emplace_back(demux_thread_func_with_params, 
                        std::ref(demux_params),     // 参数对象传引用避免拷贝
                        &raw_video_packets,
                        &raw_audio_packets);

    /**
     * 线程2：视频解码线程 (CPU密集型)
     * 职责：H.264/MPEG4等压缩视频→YUV原始帧
     * 技术细节：avcodec_send_packet() + avcodec_receive_frame()异步API
     * 内存管理：codec_params通过拷贝传递，避免主线程提前释放的竞态条件
     */
    threads.emplace_back(video_decode_to_frames_thread_func,
                        &raw_video_packets,
                        &decoded_video_frames,
                        stream_info.video_codec_params);  // 编解码器参数

    /**
     * 线程3：音频解码线程 (CPU密集型)
     * 职责：AC3/AAC等压缩音频→PCM原始音频
     * 并行设计：与视频解码完全独立，充分利用多核CPU
     */
    threads.emplace_back(audio_decode_to_frames_thread_func,
                        &raw_audio_packets,
                        &decoded_audio_frames,
                        stream_info.audio_codec_params);

    /**
     * 线程4：视频处理线程 (GPU+CPU混合)
     * 职责：OpenGL滤镜处理、旋转、变速处理
     * 技术栈：OpenGL 4.3 + GLSL着色器 + 帧缓冲对象(FBO)
     * 性能瓶颈：GPU纹理上传/下载、CPU-GPU数据传输
     */
    VideoProcessParams process_params;
    
    process_params.rotation_angle = rotation_angle;
    
    // 滤镜效果配置：支持多种图像处理算法
    process_params.enable_blur = enable_blur;          // 高斯模糊卷积
    process_params.enable_sharpen = enable_sharpen;    // 拉普拉斯锐化
    process_params.enable_grayscale = enable_grayscale; // RGB→灰度转换
    process_params.brightness = brightness;
    process_params.contrast = contrast;
    
    /**
     * 统一变速因子：确保音视频同步
     * 关键设计：所有处理模块使用相同的speed_factor，避免音画不同步
     */
    const double UNIFIED_SPEED_FACTOR = speed_factor;
    
    // 视频变速：通过帧丢弃/复制实现
    process_params.enable_speed_change = true;
    process_params.speed_factor = UNIFIED_SPEED_FACTOR;
    
    threads.emplace_back(video_process_thread_func,
                        &decoded_video_frames,
                        &processed_video_frames,
                        std::ref(process_params),       // 引用传递避免大对象拷贝
                        stream_info.video_width,
                        stream_info.video_height,
                        stream_info.video_pixel_format);

    /**
     * 线程5：音频处理线程 (CPU密集型)
     * 职责：SoundTouch变速不变调处理
     * 技术细节：WSOLA(Waveform Similarity Overlap-Add)算法
     * 内存管理：环形缓冲区避免大量内存分配
     */
    AudioProcessParams audio_process_params;
    audio_process_params.enable_speed_change = true;
    audio_process_params.speed_factor = UNIFIED_SPEED_FACTOR;  // 与视频同步
    audio_process_params.volume_gain = 1.0;  // 音量保持不变
    
    threads.emplace_back(audio_process_thread_func,
                        &decoded_audio_frames,
                        &processed_audio_frames,
                        std::ref(audio_process_params),
                        stream_info.audio_sample_rate,
                        stream_info.audio_channels,
                        AV_SAMPLE_FMT_FLTP);

    // 视频编码线程
    VideoEncoderParams video_encode_params;
    video_encode_params.width = stream_info.video_width;
    video_encode_params.height = stream_info.video_height;
    video_encode_params.fps = stream_info.video_fps;
    video_encode_params.codec_id = AV_CODEC_ID_MPEG4;
    video_encode_params.bitrate = 800000;
    
    threads.emplace_back(video_encode_thread_func,
                        &processed_video_frames,
                        &encoded_video_packets,
                        std::ref(video_encode_params));

    // 音频编码线程
    AudioEncoderParams audio_encode_params;
    audio_encode_params.sample_rate = stream_info.audio_sample_rate;
    audio_encode_params.channels = stream_info.audio_channels;
    audio_encode_params.codec_id = AV_CODEC_ID_AC3;
    audio_encode_params.bitrate = 128000;
    
    TargetAudioFormat target_audio_format = TargetAudioFormat::AC3;
    
    threads.emplace_back(audio_encode_thread_func_factory,
                        &processed_audio_frames,
                        &encoded_audio_packets,
                        target_audio_format,
                        std::ref(audio_encode_params));

    // 封装线程
    MuxerParams mux_params;
    mux_params.output_filename = output_filename;
    mux_params.format_name = "avi";
    mux_params.video_width = video_encode_params.width;
    mux_params.video_height = video_encode_params.height;
    mux_params.video_fps = video_encode_params.fps;
    mux_params.video_codec_id = AV_CODEC_ID_MPEG4;
    mux_params.audio_sample_rate = audio_encode_params.sample_rate;
    mux_params.audio_channels = audio_encode_params.channels;
    mux_params.audio_codec_id = AV_CODEC_ID_AC3;
    
    threads.emplace_back(mux_thread_func,
                        &encoded_video_packets,
                        &encoded_audio_packets,
                        std::ref(mux_params));

    std::cout << "所有线程已启动，等待完成..." << std::endl;
    std::cout << "输出文件: " << output_filename << " (AVI格式，AC3音轨)" << std::endl;
    std::cout << "变速倍数: " << UNIFIED_SPEED_FACTOR << "x" << std::endl;

    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }

    // 清理流信息
    if (stream_info.video_codec_params) {
        avcodec_parameters_free(&stream_info.video_codec_params);
    }
    if (stream_info.audio_codec_params) {
        avcodec_parameters_free(&stream_info.audio_codec_params);
    }

    std::cout << "视频转码完成！" << std::endl;
    std::cout << "输出文件: " << output_filename << std::endl;
    
    // 清理GLFW资源
    glfwTerminate();
    
    return 0;
}

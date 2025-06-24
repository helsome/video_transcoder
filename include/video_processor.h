#pragma once

#include "queue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// 视频处理参数结构
struct VideoProcessParams {
    // 旋转参数
    float rotation_angle = 0.0f;  // 旋转角度（度），0表示不旋转
    
    // 滤镜参数
    bool enable_blur = false;      // 是否启用模糊滤镜
    bool enable_sharpen = false;   // 是否启用锐化滤镜
    bool enable_grayscale = false; // 是否启用灰度滤镜
    float brightness = 1.0f;       // 亮度调整 (0.0-2.0, 1.0为原始)
    float contrast = 1.0f;         // 对比度调整 (0.0-2.0, 1.0为原始)
    
    // 输出尺寸
    int output_width = 0;     // 输出宽度，0表示使用输入宽度
    int output_height = 0;    // 输出高度，0表示使用输入高度
    
    // 视频变速参数（新增）
    bool enable_speed_change = false;  // 是否启用视频变速
    double speed_factor = 1.0;         // 变速倍数，1.0表示正常速度，>1为加速，<1为减速
};

class VideoProcessor {
public:
    VideoProcessor();
    ~VideoProcessor();

    // 初始化处理器
    bool initialize(int input_width, int input_height, AVPixelFormat input_format,
                   const VideoProcessParams& params);
    
    // 处理单帧
    bool process_frame(AVFrame* input_frame, AVFrame* output_frame);
    
    // 清理资源
    void cleanup();
    
    // 公开变速函数供线程函数使用
    bool should_process_frame(int64_t frame_pts);  // 判断是否应该处理当前帧
    bool should_duplicate_frame(int64_t frame_pts); // 判断是否需要复制帧
    int64_t calculate_new_pts(int64_t original_pts) const; // 计算新的时间戳
    int64_t get_next_frame_pts(); // 获取下一帧的线性PTS（用于复制帧）

private:
    // OpenGL上下文相关
    bool init_opengl_context();
    void cleanup_opengl_context();
    bool rotate_frame_opengl(AVFrame* input_frame, AVFrame* output_frame);
    
    // 辅助OpenGL函数
    GLuint create_shader(GLenum type, const char* source);
    GLuint create_program(const char* vertex_shader, const char* fragment_shader);
    
    // CPU实现的图像处理函数
    bool apply_blur(AVFrame* frame);
    bool apply_sharpen(AVFrame* frame);
    bool apply_grayscale(AVFrame* frame);
    bool apply_brightness_contrast(AVFrame* frame);
    
    // 辅助函数
    bool allocate_output_frame(AVFrame* frame, int width, int height, AVPixelFormat format);
    
private:
    VideoProcessParams params_;
    SwsContext* sws_ctx_;
    SwsContext* yuv_to_rgb_ctx_;  // 用于OpenGL旋转的YUV到RGB转换
    
    int input_width_;
    int input_height_;
    AVPixelFormat input_format_;
    
    int output_width_;
    int output_height_;
    AVPixelFormat output_format_;
    
    bool initialized_;
    
    // 视频变速相关成员变量（新增）
    bool speed_processing_enabled_;
    double frame_interval_;           // 原始帧间隔
    double target_frame_interval_;    // 目标帧间隔
    int64_t last_output_pts_;         // 上一个输出帧的PTS
    int frame_counter_;               // 输入帧计数器（用于丢帧判断）
    int64_t total_output_frames_;     // 输出帧计数器（用于生成新的线性时间轴）
    
    // OpenGL相关
    GLFWwindow* window_;
    bool opengl_initialized_;
    
    GLuint shader_program_;
    GLuint vertex_buffer_;
    GLuint vertex_array_;
    GLuint texture_rgb_;
    GLuint framebuffer_;
    GLuint render_texture_;
    
    // 临时缓冲区
    uint8_t* temp_buffer_;
    int temp_buffer_size_;
    uint8_t* rgb_buffer_;
    int rgb_buffer_size_;
};

// 视频处理线程函数
void video_process_thread_func(VideoFrameQueue* input_queue,
                              VideoFrameQueue* output_queue,
                              const VideoProcessParams& params,
                              int input_width, int input_height,
                              AVPixelFormat input_format);

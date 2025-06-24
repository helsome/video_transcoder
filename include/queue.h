#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 基础线程安全队列模板
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : finished_(false) {}
    ~ThreadSafeQueue() = default;

    // 禁止拷贝和赋值
    // 这样设计是为了防止多个线程意外共享同一个队列实例，导致数据竞争和同步问题。
    // 线程安全队列的设计目标是确保每个队列实例只能被一个线程使用，避免了多线程环境下的复杂性和潜在的死锁问题
    //拷贝构造函数和赋值运算符被删除，以防止意外的复制操作。
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    // 赋值运算符也被删除，确保队列实例不能被复制或赋值。
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // 向队列中推送一个元素
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!finished_) {
            queue_.push(std::move(value));
            cond_.notify_one();
        }
    }

    // 从队列中弹出一个元素，如果队列为空且未结束则阻塞等待
    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || finished_; });
        
        if (queue_.empty()) {
            return false; // 队列已结束且为空
        }
        
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // 标记队列结束，唤醒所有等待的线程
    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        cond_.notify_all();
    }

    // 检查队列是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // 获取队列大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // 检查是否已完成
    bool is_finished() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finished_;
    }

protected:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable cond_;
    std::atomic<bool> finished_;
};

// 专用的视频包队列
class VideoPacketQueue : public ThreadSafeQueue<AVPacket*> {
public:
    VideoPacketQueue() = default;
    ~VideoPacketQueue() {
        clear();
    }

    // 清空队列并释放所有包
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            //AVPacket 是一个结构体，用于存储压缩数据，通常由解复用器导出并传递给解码器，或由编码器输出后传递给复用器。
            AVPacket* packet = queue_.front();
            queue_.pop();
            if (packet) {
                av_packet_free(&packet);
            }
        }
    }
};

// 专用的音频包队列
class AudioPacketQueue : public ThreadSafeQueue<AVPacket*> {
public:
    AudioPacketQueue() = default;
    ~AudioPacketQueue() {
        clear();
    }

    // 清空队列并释放所有包
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            AVPacket* packet = queue_.front();
            queue_.pop();
            if (packet) {
                av_packet_free(&packet);
            }
        }
    }
};

// 专用的视频帧队列
class VideoFrameQueue : public ThreadSafeQueue<AVFrame*> {
public:
    VideoFrameQueue() = default;
    ~VideoFrameQueue() {
        clear();
    }

    // 清空队列并释放所有帧
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            AVFrame* frame = queue_.front();
            queue_.pop();
            if (frame) {
                av_frame_free(&frame);
            }
        }
    }
};

// 专用的音频帧队列
class AudioFrameQueue : public ThreadSafeQueue<AVFrame*> {
public:
    AudioFrameQueue() = default;
    ~AudioFrameQueue() {
        clear();
    }

    // 清空队列并释放所有帧
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            AVFrame* frame = queue_.front();
            queue_.pop();
            if (frame) {
                av_frame_free(&frame);
            }
        }
    }
};

// 编码后的视频包队列
class EncodedVideoPacketQueue : public ThreadSafeQueue<AVPacket*> {
public:
    EncodedVideoPacketQueue() = default;
    ~EncodedVideoPacketQueue() {
        clear();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            AVPacket* packet = queue_.front();
            queue_.pop();
            if (packet) {
                av_packet_free(&packet);
            }
        }
    }
};

// 编码后的音频包队列
class EncodedAudioPacketQueue : public ThreadSafeQueue<AVPacket*> {
public:
    EncodedAudioPacketQueue() = default;
    ~EncodedAudioPacketQueue() {
        clear();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            AVPacket* packet = queue_.front();
            queue_.pop();
            if (packet) {
                av_packet_free(&packet);
            }
        }
    }
};

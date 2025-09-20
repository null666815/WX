#pragma once
#include <vector>
#include <queue>
#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

// 使用std::thread的线程池实现

// 任务执行结果
enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED
};

// 通用任务基类
class TaskBase {
public:
    virtual ~TaskBase() = default;
    virtual void execute() = 0;
    virtual void onComplete() {}
    virtual void onError() {}

    TaskStatus getStatus() const { return status_; }
    void setStatus(TaskStatus status) { status_ = status; }

protected:
    std::atomic<TaskStatus> status_;
};

// 函数任务类（改进版）
class FunctionTask : public TaskBase {
public:
    FunctionTask(std::function<void()> func) : function_(std::move(func)) {}

    void execute() override {
        status_ = TaskStatus::RUNNING;
        try {
            if (function_) {
                function_();
            }
            status_ = TaskStatus::COMPLETED;
            onComplete();
        } catch (...) {
            status_ = TaskStatus::FAILED;
            onError();
        }
    }

private:
    std::function<void()> function_;
};

// 循环任务类（改进版）
class LoopTask : public TaskBase {
public:
    LoopTask(std::function<void(void*, size_t)> func, void* data, size_t iterations)
        : function_(func), data_(data), iterations_(iterations) {}

    void execute() override {
        status_ = TaskStatus::RUNNING;
        try {
            for (size_t i = 0; i < iterations_; ++i) {
                if (function_) {
                    function_(data_, i);
                }
            }
            status_ = TaskStatus::COMPLETED;
            onComplete();
        } catch (...) {
            status_ = TaskStatus::FAILED;
            onError();
        }
    }

private:
    std::function<void(void*, size_t)> function_;
    void* data_ = nullptr;
    size_t iterations_;
};

class ThreadPool {
public:
    ThreadPool(size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPool();

    // 任务提交（新API）
    void submit(std::shared_ptr<TaskBase> task);
    template<typename F>
    void submit(F&& func);
    template<typename F, typename... Args>
    void submit(F&& func, Args&&... args);

    // 批处理
    void submitBatch(const std::vector<std::shared_ptr<TaskBase>>& tasks);

    // 控制
    void start();
    void stop();
    void waitForCompletion();

    // 状态查询
    size_t getThreadCount() const { return worker_threads_.size(); }
    size_t getQueuedTasks() const;
    size_t getCompletedTasks() const { return completed_tasks_.load(); }
    size_t getActiveTasks() const { return active_tasks_.load(); }
    bool isRunning() const { return running_; }

    // 统计
    void printStatistics() const;
    double getAverageExecutionTime() const;

private:
    void workerLoop();
    void updateStatistics();

    // 线程和同步
    std::vector<std::thread> worker_threads_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool running_ = false;

    // 任务队列
    std::queue<std::shared_ptr<TaskBase>> task_queue_;

    // 统计
    std::atomic<size_t> completed_tasks_{0};
    std::atomic<size_t> active_tasks_{0};
    size_t failed_tasks_ = 0;
    std::chrono::steady_clock::duration total_execution_time_{0};
};

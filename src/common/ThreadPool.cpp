#include "ThreadPool.hpp"
#include <iostream>
#include <chrono>

/**
 * ===============================================================================
 * ThreadPool 构造函数 - std::thread实现
 * ===============================================================================
 */
ThreadPool::ThreadPool(size_t threadCount) {
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0) threadCount = 4;
    }

    std::cout << "[ThreadPool] Creating pool with " << threadCount << " worker threads" << std::endl;
    worker_threads_.reserve(threadCount);

    start();
}

/**
 * ThreadPool 析构函数
 */
ThreadPool::~ThreadPool() {
    stop();
    std::cout << "[ThreadPool] Destroyed - Processed " << completed_tasks_.load() << " tasks" << std::endl;
}

/**
 * 启动线程池
 */
void ThreadPool::start() {
    if (running_) return;

    running_ = true;

    std::cout << "[ThreadPool] Starting " << worker_threads_.capacity() << " worker threads..." << std::endl;

    for (size_t i = 0; i < worker_threads_.capacity(); ++i) {
        worker_threads_.emplace_back(&ThreadPool::workerLoop, this);
    }

    std::cout << "[ThreadPool] Started successfully" << std::endl;
}

/**
 * 停止线程池
 */
void ThreadPool::stop() {
    if (!running_) return;

    running_ = false;
    condition_.notify_all();

    std::cout << "[ThreadPool] Stopping worker threads..." << std::endl;

    for (auto& worker : worker_threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    worker_threads_.clear();
    std::cout << "[ThreadPool] Stopped successfully" << std::endl;
}

/**
 * 提交通用任务
 */
void ThreadPool::submit(std::shared_ptr<TaskBase> task) {
    if (!running_) {
        std::cout << "[ThreadPool] Pool not running - task ignored" << std::endl;
        return;
    }

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
    }

    condition_.notify_one();
    std::cout << "[ThreadPool] Task submitted - Queue size: " << getQueuedTasks() << std::endl;
}

/**
 * 使用函数对象提交任务
 */
template<typename F>
void ThreadPool::submit(F&& func) {
    auto task = std::make_shared<FunctionTask>(std::forward<F>(func));
    submit(task);
}

template<typename F, typename... Args>
void ThreadPool::submit(F&& func, Args&&... args) {
    auto lambda = [=]() {
        func(std::forward<Args>(args)...);
    };
    submit(std::move(lambda));
}

/**
 * 批量提交任务
 */
void ThreadPool::submitBatch(const std::vector<std::shared_ptr<TaskBase>>& tasks) {
    if (!running_) return;

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        for (const auto& task : tasks) {
            task_queue_.push(task);
        }
    }

    condition_.notify_all();
    std::cout << "[ThreadPool] Batch submitted - " << tasks.size() << " tasks" << std::endl;
}

/**
 * 等待队列中所有任务完成
 */
void ThreadPool::waitForCompletion() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    condition_.wait(lock, [this]() {
        return task_queue_.empty() && active_tasks_.load() == 0;
    });
    std::cout << "[ThreadPool] All queued tasks completed" << std::endl;
}

/**
 * 获取队列中任务数量
 */
size_t ThreadPool::getQueuedTasks() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

/**
 * 打印统计信息
 */
void ThreadPool::printStatistics() const {
    std::cout << "=== ThreadPool 统计信息 ===" << std::endl;
    std::cout << "线程数量: " << getThreadCount() << std::endl;
    std::cout << "已处理任务: " << getCompletedTasks() << std::endl;
    std::cout << "失败任务: " << failed_tasks_ << std::endl;
    std::cout << "当前活跃任务: " << getActiveTasks() << std::endl;
    std::cout << "队列中任务: " << getQueuedTasks() << std::endl;
    std::cout << "平均执行时间: " << getAverageExecutionTime() << " ms" << std::endl;
    std::cout << "运行状态: " << (isRunning() ? "运行中" : "已停止") << std::endl;
    std::cout << std::endl;
}

/**
 * 获取平均执行时间
 */
double ThreadPool::getAverageExecutionTime() const {
    size_t completed = getCompletedTasks();
    if (completed == 0) return 0.0;

    return std::chrono::duration<double, std::milli>(total_execution_time_).count() / completed;
}

/**
 * Worker线程主循环
 */
void ThreadPool::workerLoop() {
    while (running_) {
        std::shared_ptr<TaskBase> task;

        // 获取任务
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this]() {
                return !running_ || !task_queue_.empty();
            });

            if (!running_ || task_queue_.empty()) {
                continue;
            }

            task = task_queue_.front();
            task_queue_.pop();
            active_tasks_++;
        }

        if (task) {
            // 执行任务
            auto start_time = std::chrono::steady_clock::now();

            try {
                task->execute();
                completed_tasks_++;
            } catch (...) {
                failed_tasks_++;
                std::cout << "[ThreadPool] Task execution failed" << std::endl;
            }

            auto end_time = std::chrono::steady_clock::now();
            total_execution_time_ += end_time - start_time;

            active_tasks_--;
        }
    }
}

/**
 * 更新统计信息（保留接口）
 */
void ThreadPool::updateStatistics() {
    // 统计信息已在上面自动更新
}


/**
 * ===============================================================================
 * 模板函数在需要时自动实例化（C++11）
 * ===============================================================================
 */

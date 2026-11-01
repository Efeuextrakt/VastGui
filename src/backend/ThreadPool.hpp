#pragma once
// Fixed-size worker pool built on std::jthread. Tasks receive the worker's
// stop_token so long-running work (child processes, HTTP) can be cancelled
// when the pool shuts down.

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <vector>

namespace vast {

class ThreadPool {
public:
    using Task = std::function<void(std::stop_token)>;

    explicit ThreadPool(std::size_t threads = 4) {
        if (threads == 0) threads = 1;
        workers_.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) { workerLoop(st); });
        }
    }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Returns false (task dropped) once the pool is shutting down.
    bool post(Task task) {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) return false;
            queue_.push_back(std::move(task));
        }
        cv_.notify_one();
        return true;
    }

    // Runs `f(stop_token)` on a worker and returns a future for its result.
    template <class F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F, std::stop_token>> {
        using R = std::invoke_result_t<F, std::stop_token>;
        auto task = std::make_shared<std::packaged_task<R(std::stop_token)>>(std::forward<F>(f));
        auto fut = task->get_future();
        post([task](std::stop_token st) { (*task)(st); });
        return fut;
    }

    [[nodiscard]] std::size_t queued() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t size() const { return workers_.size(); }

    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) return;
            stopped_ = true;
            queue_.clear();
        }
        for (auto& w : workers_) w.request_stop();
        cv_.notify_all();
        workers_.clear();  // joins
    }

private:
    void workerLoop(std::stop_token st) {
        while (true) {
            Task task;
            {
                std::unique_lock lock(mutex_);
                if (!cv_.wait(lock, st, [this] { return !queue_.empty(); })) return;
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            task(st);
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<Task> queue_;
    bool stopped_ = false;
    std::vector<std::jthread> workers_;
};

// Simple MPSC queue drained by the UI thread each frame.
template <class T>
class ResultQueue {
public:
    void push(T v) {
        std::lock_guard lock(mutex_);
        items_.push_back(std::move(v));
    }
    [[nodiscard]] std::vector<T> drain() {
        std::lock_guard lock(mutex_);
        std::vector<T> out(std::make_move_iterator(items_.begin()), std::make_move_iterator(items_.end()));
        items_.clear();
        return out;
    }

private:
    std::mutex mutex_;
    std::deque<T> items_;
};

}  // namespace vast

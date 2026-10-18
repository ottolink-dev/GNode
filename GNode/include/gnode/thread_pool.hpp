/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/**
 * @file thread_pool.hpp
 * @brief Thread-pool implementation for concurrent task execution.
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace gnode
{

/**
 * @class ThreadPool
 * @brief A lightweight, thread-safe task queue and worker pool.
 */
class ThreadPool
{
public:
  /**
   * @brief Construct a ThreadPool with a given number of worker threads.
   * @param num_threads Number of worker threads to spawn. Defaults to
   * std::thread::hardware_concurrency().
   */
  explicit ThreadPool(size_t num_threads = 0);

  /**
   * @brief Destructor stops worker threads and joins them.
   */
  ~ThreadPool();

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  /**
   * @brief Enqueue a callable task for asynchronous execution by the pool.
   * @tparam F Callable type.
   * @tparam Args Argument types.
   * @param f Function to invoke.
   * @param args Arguments to forward.
   * @return std::future holding the return value of the task.
   */
  template <typename F, typename... Args>
  auto enqueue(F &&f, Args &&...args)
      -> std::future<typename std::invoke_result<F, Args...>::type>
  {
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();

    {
      std::unique_lock<std::mutex> lock(this->queue_mutex);
      if (this->stop) throw std::runtime_error("enqueue on stopped ThreadPool");
      this->tasks.emplace([task]() { (*task)(); });
    }

    this->cv.notify_one();

    return res;
  }

  /**
   * @brief Get the number of worker threads in the pool.
   */
  size_t get_thread_count() const { return this->workers.size(); }

private:
  std::vector<std::thread>          workers;
  std::queue<std::function<void()>> tasks;

  mutable std::mutex      queue_mutex;
  std::condition_variable cv;
  bool                    stop = false;
};

} // namespace gnode

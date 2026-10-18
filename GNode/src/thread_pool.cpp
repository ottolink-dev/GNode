/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

#include "gnode/thread_pool.hpp"

namespace gnode
{

ThreadPool::ThreadPool(size_t num_threads)
{
  if (num_threads == 0)
  {
    num_threads = std::thread::hardware_concurrency();

    if (num_threads == 0) num_threads = 2;
  }

  this->workers.reserve(num_threads);

  for (size_t i = 0; i < num_threads; ++i)
  {
    this->workers.emplace_back(
        [this]()
        {
          while (true)
          {
            std::function<void()> task;

            {
              std::unique_lock<std::mutex> lock(this->queue_mutex);

              this->cv.wait(lock,
                            [this]()
                            { return this->stop || !this->tasks.empty(); });

              if (this->stop && this->tasks.empty()) return;

              task = std::move(this->tasks.front());
              this->tasks.pop();
            }

            task();
          }
        });
  }
}

ThreadPool::~ThreadPool()
{
  {
    std::unique_lock<std::mutex> lock(this->queue_mutex);

    this->stop = true;
  }

  this->cv.notify_all();

  for (std::thread &worker : this->workers)
  {
    if (worker.joinable()) worker.join();
  }
}

} // namespace gnode

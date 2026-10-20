/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/**
 * @file graph_worker.hpp
 * @brief Background asynchronous graph update worker and job manager.
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace gnode
{

class Graph; // forward

/**
 * @class GraphWorker
 * @brief Updates graphs on dedicated background threads, orchestrating request
 * debouncing, dirty coalescing, cooperative cancellation, and completion
 * callbacks.
 */
class GraphWorker
{
public:
  GraphWorker();
  ~GraphWorker();

  /**
   * @brief Queues the specified nodes and their downstream dependencies, or the
   * whole graph if p_node_ids is null. Cancels any update of that graph in
   * progress: it will resume with what was left plus the newly requested nodes.
   * Never blocks.
   * @param graph The graph to update.
   * @param p_node_ids Optional subset of dirty node IDs. If null, the entire
   * graph is scheduled.
   * @return true if the job was queued/accepted, false if background updates
   * are disabled and the caller should update synchronously.
   */
  bool request(Graph &graph, const std::vector<std::string> *p_node_ids = nullptr);

  /**
   * @brief Cancels the update in progress and waits for the thread to finish.
   * Graph modifications can safely be performed once this returns.
   */
  void stop();

  /**
   * @brief Drops what is queued for this graph (or for all graphs if p_graph is
   * nullptr) and stops the worker.
   */
  void cancel(const Graph *p_graph = nullptr);

  /**
   * @brief Cancels the update in progress without waiting, and holds what is
   * left until the next request resumes it.
   * @return true if a job was running or queued, false if nothing was held.
   */
  bool hold();

  /**
   * @brief Stops background processing, then computes all remaining queued jobs
   * synchronously before returning.
   */
  void finish();

  /**
   * @brief Dispatches a callable to the caller/main thread using the registered
   * post_to_caller hook (or runs immediately if no hook is set).
   */
  void post(std::function<void()> fct);

  /**
   * @brief Returns whether the current thread is an update/worker thread.
   */
  static bool in_worker_thread();

  // --- Static Hook Callbacks ---

  /** Predicate determining if background updates are enabled (defaults to true). */
  static inline std::function<bool()> enabled = []() { return true; };

  /** Post callback to caller thread event loop (e.g. Qt QMetaObject::invokeMethod). */
  static inline std::function<void(std::function<void()>)> post_to_caller;

  /** Setup callback executed on pool worker threads before updating nodes (e.g. OpenMP init). */
  static inline std::function<void()> thread_setup;

  // --- Instance Callbacks ---

  /** Callback invoked when an update pass terminates with an unhandled exception. */
  std::function<void(std::exception_ptr)> failed;

  /** Callback invoked when hold state changes (true while held, false when resumed). */
  std::function<void(bool)> held_changed;

private:
  struct Job
  {
    std::weak_ptr<Graph>  graph = {};
    std::set<std::string> node_ids = {};
    bool                  all = false;
  };

  bool                      check_cancel(int pass, const std::string &node_id);
  void                      end_pass();
  std::deque<Job>::iterator find_job(const Graph &graph);
  void                      schedule_start_next();
  void                      start_next();

  // Caller thread state
  std::deque<Job>          jobs;
  std::shared_ptr<Graph>   graph;
  std::vector<std::string> planned;
  std::set<std::string>    started;
  std::exception_ptr       error;
  std::thread              thread;
  bool                     start_next_pending = false;
  bool                     held = false;

  // Shared coordination state
  std::mutex        started_mutex;
  std::atomic<bool> cancelled = false;
  std::atomic<int>  pass = 0;

  // Flush coordination state during finish()
  std::atomic<bool>                is_finishing = false;
  std::mutex                       flush_mutex;
  std::vector<std::function<void()>> flush_callbacks;

  std::shared_ptr<int> alive = std::make_shared<int>();
};

} // namespace gnode

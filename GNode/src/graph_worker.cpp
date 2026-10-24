/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

#include "gnode/graph_worker.hpp"
#include "gnode/graph.hpp"
#include "gnode/logger.hpp"
#include "gnode/node.hpp"

#include <algorithm>

namespace gnode
{

namespace
{
thread_local bool is_worker_thread = false;
} // namespace

GraphWorker::GraphWorker() = default;

GraphWorker::~GraphWorker()
{
  this->cancelled = true;

  if (this->thread.joinable()) this->thread.join();
}

bool GraphWorker::check_cancel(int pass, const std::string &node_id)
{
  if (!is_worker_thread)
  {
    is_worker_thread = true;
    if (this->thread_setup) this->thread_setup();
  }

  if (pass != this->pass.load() || this->cancelled.load()) return true;

  {
    std::lock_guard<std::mutex> lock(this->started_mutex);
    this->started.insert(node_id);
  }

  return false;
}

void GraphWorker::cancel(const Graph *p_graph)
{
  Logger::log()->trace("GraphWorker::cancel: graph {}",
                       p_graph ? p_graph->get_id() : "all");

  this->stop();
  std::erase_if(this->jobs,
                [p_graph](const Job &job)
                { return !p_graph || job.graph.lock().get() == p_graph; });

  if (this->jobs.empty() && std::exchange(this->held, false) &&
      this->held_changed)
    this->held_changed(false);
}

void GraphWorker::end_pass()
{
  if (!this->thread.joinable()) return;

  this->thread.join();
  ++this->pass;

  auto graph = std::exchange(this->graph, nullptr);
  graph->cancel_update_callback = nullptr;

  std::set<std::string> left;

  for (const auto &node_id : this->planned)
  {
    if (!this->started.contains(node_id))
    {
      left.insert(node_id);
      if (auto *p_node = graph->get_node(node_id))
        p_node->on_async_compute_cancelled();
    }
  }

  Logger::log()->trace("GraphWorker::end_pass: pass {} finished ({} computed, "
                       "{} left, has_error={})",
                       this->pass - 1,
                       this->started.size(),
                       left.size(),
                       this->error != nullptr);

  auto job = this->find_job(*graph);

  if (this->error)
  {
    this->jobs.clear();

    if (this->failed) this->failed(std::exchange(this->error, nullptr));
  }
  else if (!left.empty() || job != this->jobs.end())
  {
    Job resumed = job != this->jobs.end() ? std::move(*job)
                                          : Job{.graph = graph};

    if (job != this->jobs.end()) this->jobs.erase(job);

    if (!resumed.all) resumed.node_ids.insert(left.begin(), left.end());

    for (auto it = this->jobs.begin(); it != this->jobs.end();)
    {
      if (it->graph.lock() == graph)
      {
        if (it->all)
        {
          resumed.all = true;
          resumed.node_ids.clear();
        }
        else if (!resumed.all)
        {
          resumed.node_ids.insert(it->node_ids.begin(), it->node_ids.end());
        }

        it = this->jobs.erase(it);
      }
      else
      {
        ++it;
      }
    }

    this->jobs.push_front(std::move(resumed));
  }
  else if (graph->update_finished)
  {
    Logger::log()->trace(
        "GraphWorker::end_pass: update completely finished for graph '{}'",
        graph->get_id());
    graph->update_finished();
  }

  if (this->held && !this->jobs.empty() && this->held_changed)
    this->held_changed(true);

  this->cancelled = false;
}

void GraphWorker::finish()
{
  Logger::log()->trace("GraphWorker::finish: flushing {} queued jobs",
                       this->jobs.size());

  this->stop();

  this->is_finishing = true;

  while (true)
  {
    if (this->jobs.empty())
    {
      std::vector<std::function<void()>> callbacks;
      {
        std::lock_guard<std::mutex> lock(this->flush_mutex);
        callbacks = std::move(this->flush_callbacks);
        this->flush_callbacks.clear();
      }

      if (callbacks.empty()) break;

      for (auto &callback : callbacks)
        callback();

      continue;
    }

    Job job = std::move(this->jobs.front());
    this->jobs.pop_front();

    auto graph = job.graph.lock();
    if (!graph) continue;

    std::vector<std::string> ids;
    if (job.all)
    {
      for (const auto &[node_id, _] : graph->get_nodes())
        ids.push_back(node_id);
    }
    else
    {
      for (const auto &node_id : job.node_ids)
        if (graph->get_node(node_id)) ids.push_back(node_id);
    }

    auto planned = job.all ? graph->topological_sort(ids)
                           : graph->get_nodes_to_update(ids);

    if (planned.empty())
    {
      if (graph->update_finished) graph->update_finished();
      continue;
    }

    for (const auto &node_id : planned)
      if (auto *p_node = graph->get_node(node_id))
        p_node->on_pre_async_compute();

    // Setup callback to ensure thread setup runs on worker threads during
    // finish()
    graph->cancel_update_callback = [this](const std::string &)
    {
      if (!is_worker_thread)
      {
        is_worker_thread = true;
        if (this->thread_setup) this->thread_setup();
      }
      return false;
    };

    graph->execute_parallel(planned);

    graph->cancel_update_callback = nullptr;

    if (graph->update_finished) graph->update_finished();

    // Drain any callbacks posted during parallel execution (e.g. broadcast
    // updates)
    std::vector<std::function<void()>> callbacks;
    {
      std::lock_guard<std::mutex> lock(this->flush_mutex);
      callbacks = std::move(this->flush_callbacks);
      this->flush_callbacks.clear();
    }

    for (auto &callback : callbacks)
      callback();
  }

  this->is_finishing = false;

  if (std::exchange(this->held, false) && this->held_changed)
    this->held_changed(false);
}

bool GraphWorker::hold()
{
  const bool busy = this->thread.joinable() || !this->jobs.empty();

  Logger::log()->trace("GraphWorker::hold: busy={}", busy);

  if (!busy) return false;

  this->held = true;

  if (this->thread.joinable())
    this->cancelled = true;
  else if (this->held_changed)
    this->held_changed(true);

  return true;
}

bool GraphWorker::in_worker_thread() { return is_worker_thread; }

std::deque<GraphWorker::Job>::iterator GraphWorker::find_job(const Graph &graph)
{
  return std::ranges::find_if(this->jobs,
                              [&graph](const Job &job)
                              { return job.graph.lock().get() == &graph; });
}

void GraphWorker::post(std::function<void()> fct)
{
  if (this->is_finishing)
  {
    std::lock_guard<std::mutex> lock(this->flush_mutex);
    this->flush_callbacks.push_back(
        [alive = std::weak_ptr<int>(this->alive), fct = std::move(fct)]()
        {
          if (alive.lock()) fct();
        });
    return;
  }

  if (this->post_to_caller)
  {
    this->post_to_caller(
        [alive = std::weak_ptr<int>(this->alive), fct = std::move(fct)]()
        {
          if (alive.lock()) fct();
        });
  }
  else
  {
    // If no caller loop is provided (e.g. unit tests or CLI), run posted task
    // in a detached thread to prevent deadlock if posted from inside
    // this->thread
    std::thread(
        [alive = std::weak_ptr<int>(this->alive), fct = std::move(fct)]()
        {
          if (alive.lock()) fct();
        })
        .detach();
  }
}

bool GraphWorker::request(Graph                          &graph,
                          const std::vector<std::string> *p_node_ids)
{
  if (!this->enabled || !this->enabled())
  {
    this->finish();
    return false;
  }

  auto job = this->find_job(graph);

  if (job == this->jobs.end())
    job = this->jobs.insert(job, Job{.graph = graph.weak_from_this()});

  if (!p_node_ids)
  {
    job->all = true;
    job->node_ids.clear();
  }
  else if (!job->all)
  {
    job->node_ids.insert(p_node_ids->begin(), p_node_ids->end());
  }

  Logger::log()->trace("GraphWorker::request: graph '{}' (all={}, {} nodes)",
                       graph.get_id(),
                       !p_node_ids,
                       p_node_ids ? p_node_ids->size() : 0);

  if (this->graph.get() == &graph) this->cancelled = true;

  if (std::exchange(this->held, false) && this->held_changed)
    this->held_changed(false);

  this->schedule_start_next();
  return true;
}

void GraphWorker::schedule_start_next()
{
  if (this->start_next_pending) return;

  this->start_next_pending = true;

  this->post(
      [this]()
      {
        this->start_next_pending = false;
        this->start_next();
      });
}

void GraphWorker::start_next()
{
  if (this->held) return;

  while (!this->thread.joinable() && !this->jobs.empty())
  {
    Job job = std::move(this->jobs.front());
    this->jobs.pop_front();

    auto graph = job.graph.lock();
    if (!graph) continue;

    std::vector<std::string> ids;
    for (const auto &[node_id, _] : graph->get_nodes())
      if (job.all || job.node_ids.contains(node_id)) ids.push_back(node_id);

    if (ids.empty())
    {
      if (graph->update_finished) graph->update_finished();
      continue;
    }

    if (!job.all)
      for (const auto &node_id : ids)
        graph->get_node(node_id)->is_dirty = false;

    auto planned = job.all ? graph->topological_sort(ids)
                           : graph->get_nodes_to_update(ids);

    if (planned.empty())
    {
      if (graph->update_finished) graph->update_finished();
      continue;
    }

    this->graph = graph;
    this->planned = std::move(planned);
    {
      std::lock_guard<std::mutex> lock(this->started_mutex);
      this->started.clear();
    }
    this->error = nullptr;
    const int pass = ++this->pass;

    // Snapshot planned nodes upfront on caller thread using virtual lifecycle
    // hook
    for (const auto &node_id : this->planned)
      if (auto *p_node = graph->get_node(node_id))
        p_node->on_pre_async_compute();

    Logger::log()->trace("GraphWorker::start_next: starting pass {} for graph "
                         "'{}' with {} planned nodes",
                         pass,
                         graph->get_id(),
                         this->planned.size());

    graph->cancel_update_callback = [this, pass](const std::string &node_id)
    { return this->check_cancel(pass, node_id); };

    this->thread = std::thread(
        [this, graph, dirty_ids = this->planned, pass]()
        {
          try
          {
            graph->execute_parallel(dirty_ids);
          }
          catch (...)
          {
            this->error = std::current_exception();
          }

          this->post(
              [this, pass]()
              {
                if (pass != this->pass) return;
                this->end_pass();
                this->start_next();
              });
        });
  }
}

void GraphWorker::stop()
{
  if (!this->thread.joinable()) return;

  Logger::log()->trace("GraphWorker::stop: stopping update thread");

  this->cancelled = true;
  this->end_pass();

  this->schedule_start_next();
}

} // namespace gnode

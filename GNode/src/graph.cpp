/* Copyright (c) 2023 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wrange-loop-construct"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wreturn-type"

#include "demekgraph/updated/include/graph.hpp"
#include "demekgraph/updated/include/layout.hpp"

#pragma GCC diagnostic pop

#include "gnode/graph.hpp"
#include "gnode/logger.hpp"
#include "gnode/thread_pool.hpp"

namespace gnode
{

void helper_mark_dirty(
    const std::string                                     &node_id,
    std::vector<std::string>                              &visited,
    const std::map<std::string, std::vector<std::string>> &connectivity_dw)
{
  if (contains(visited, node_id)) return;

  visited.push_back(node_id);
  for (const auto &dw_id : connectivity_dw.at(node_id))
    helper_mark_dirty(dw_id, visited, connectivity_dw);
}

std::string Graph::add_node(const std::shared_ptr<Node> &p_node,
                            const std::string           &id)
{
  std::string node_id = id;

  // Use node pointer as ID if none is provided
  if (node_id.empty()) node_id = std::to_string(id_count++);

  // Check if the ID is available
  if (!this->is_node_id_available(node_id))
    throw std::runtime_error("Node ID already used: " + node_id);

  // Add the node to the map and store the ID within the node (in case of)
  this->nodes[node_id] = p_node;
  p_node->set_id(node_id);

  // keep track of the parent graph
  p_node->set_p_graph(this);

  return node_id;
}

void Graph::clear()
{
  this->nodes.clear();
  this->links.clear();
  this->id_count = 0;
}

std::vector<Point> Graph::compute_graph_layout_sugiyama()
{
  std::vector<Point> points;
  const size_t       num_nodes = this->nodes.size();

  // Create an adjacency list for the Sugiyama procedure
  std::vector<std::vector<size_t>> adj(num_nodes);

  // Build a node ID to node index map
  std::map<std::string, size_t> node_idx;
  size_t                        idx = 0;

  for (const auto &[nid, p_node] : this->nodes)
    node_idx[nid] = idx++;

  // Populate the adjacency list using downstream connectivity
  const auto connectivity = this->get_connectivity_downstream();

  for (const auto &[nid, to_ids] : connectivity)
  {
    size_t from_idx = node_idx.at(nid);
    for (const auto &to_id : to_ids)
      adj[from_idx].push_back(node_idx.at(to_id));
  }

  // Build the graph using the adjacency list
  graph_builder gb;

  for (size_t i = 0; i < adj.size(); ++i)
    for (size_t j : adj[i])
      gb.add_edge(i, j);

  graph graph = gb.build();

  // Set Sugiyama layout attributes
  attributes attr;
  attr.node_size = 0;
  attr.node_dist = 1;
  attr.layer_dist = 1;

  // Compute Sugiyama layout
  sugiyama_layout layout(graph, attr);

  // Convert layout vertices to points (reverse x and y for horizontal layout)
  points.reserve(layout.vertices().size());

  for (const auto &vertex : layout.vertices())
    points.emplace_back(vertex.pos.y, vertex.pos.x);

  // Make coordinates start at zero
  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();

  for (const auto &p : points)
  {
    min_x = std::min(min_x, p.x);
    min_y = std::min(min_y, p.y);
  }

  for (auto &p : points)
  {
    p.x -= min_x;
    p.y -= min_y;
  }

  return points;
}

void Graph::export_to_graphviz(const std::string &fname,
                               const std::string &graph_label)
{
  std::ofstream file(fname);

  if (!file.is_open())
    throw std::runtime_error("Failed to open file: " + fname);

  file << "digraph root {\n";
  file << "label=\"" << graph_label << "\";\n";
  file << "labelloc=\"t\";\n";
  file << "rankdir=TD;\n";
  file << "ranksep=0.5;\n";
  file << "node [shape=record];\n";

  // Output nodes with their labels
  for (const auto &[id, p_node] : this->nodes)
    file << id << " [label=\"" << p_node->get_label() << "\"];\n";

  // Output edges
  const auto connectivity = this->get_connectivity_downstream();

  for (const auto &[from_id, to_ids] : connectivity)
    for (const auto &to_id : to_ids)
      file << from_id << " -> " << to_id << ";\n";

  file << "}\n";
}

void Graph::export_to_mermaid(const std::string &fname,
                              const std::string &graph_label)
{
  std::fstream f;
  f.open(fname, std::ios::out);

  f << "---" << std::endl;
  f << "title: " << graph_label << std::endl;
  f << "---" << std::endl;
  f << "flowchart LR" << std::endl;

  // nodes
  for (const auto &[id, p_node] : this->nodes)
    f << "    " << id << "([" << p_node->get_label() << "])\n";

  // Output edges
  const auto connectivity = this->get_connectivity_downstream();

  for (const auto &[from_id, to_ids] : connectivity)
    for (const auto &to_id : to_ids)
      f << from_id << " --> " << to_id << ";\n";

  f.close();
}

std::map<std::string, std::vector<std::string>> Graph::
    get_connectivity_downstream() const
{
  std::map<std::string, std::vector<std::string>> connectivity;

  // to get all the nodes in the mapping, even if they have no node
  // downstream
  for (const auto &[nid, _] : this->nodes)
    connectivity[nid] = {};

  for (const auto &link : this->links)
    connectivity[link.from].push_back(link.to);

  return connectivity;
}

std::vector<std::string> Graph::get_connectivity_downstream(
    const std::string &node_id) const
{
  std::vector<std::string> connectivity = {};

  for (const auto &link : this->links)
  {
    if (link.from == node_id) connectivity.push_back(link.to);
  }

  return connectivity;
}

std::map<std::string, std::vector<std::string>> Graph::
    get_connectivity_upstream() const
{
  std::map<std::string, std::vector<std::string>> connectivity;

  // to get all the nodes in the mapping, even if they have no node
  // upstream
  for (const auto &[nid, _] : this->nodes)
    connectivity[nid] = {};

  for (const auto &link : this->links)
    connectivity[link.to].push_back(link.from);

  return connectivity;
}

std::vector<std::string> Graph::get_connectivity_upstream(
    const std::string &node_id) const
{
  std::vector<std::string> connectivity = {};

  for (const auto &link : this->links)
  {
    if (link.to == node_id) connectivity.push_back(link.from);
  }

  return connectivity;
}

std::vector<LinkView> Graph::get_link_views(const std::string &node_id) const
{
  std::vector<gnode::LinkView> link_views = {};

  for (const auto &link : this->get_links())
  {
    if (link.from == node_id || link.to == node_id)
    {
      Node *p_from = this->get_node(link.from);
      Node *p_to = this->get_node(link.to);

      if (!p_from || !p_to) continue;

      LinkView data(link, *p_from, *p_to);
      link_views.push_back(data);
    }
  }

  return link_views;
}

std::vector<std::string> Graph::get_nodes_to_update(
    const std::vector<std::string> &node_ids)
{
  // --- validate input nodes

  for (const auto &node_id : node_ids)
  {
    if (this->is_node_id_available(node_id))
      throw std::runtime_error("Graph::get_nodes_to_update: unknown node id: " +
                               node_id);
  }

  // --- check upstream dependencies

  auto connectivity_up = this->get_connectivity_upstream();

  for (const auto &node_id : node_ids)
  {
    for (const auto &id_up : connectivity_up[node_id])
    {
      Node *p_node = this->get_node(id_up);

      if (p_node && p_node->is_dirty)
      {
        Logger::log()->trace("Graph::update: no update of the graph");
        return {};
      }
    }
  }

  // --- collect downstream dirty nodes

  std::vector<std::string> dirty_node_ids = {};

  auto connectivity_dw = this->get_connectivity_downstream();

  for (const auto &node_id : node_ids)
    helper_mark_dirty(node_id, dirty_node_ids, connectivity_dw);

  // --- remove duplicates

  std::sort(dirty_node_ids.begin(), dirty_node_ids.end());

  dirty_node_ids.erase(
      std::unique(dirty_node_ids.begin(), dirty_node_ids.end()),
      dirty_node_ids.end());

  // --- topological ordering

  return topological_sort(dirty_node_ids);
}

std::vector<std::string> Graph::get_nodes_to_update(const std::string &node_id)
{
  return this->get_nodes_to_update(std::vector<std::string>{node_id});
}

bool Graph::has_cycle() const
{
  std::vector<std::string> all_nodes;

  for (const auto &[id, _] : nodes)
    all_nodes.push_back(id);

  auto sorted = topological_sort(all_nodes);

  return sorted.size() != all_nodes.size();
}

bool Graph::is_node_id_available(const std::string &node_id)
{
  return !this->nodes.contains(node_id);
}

bool Graph::is_reachable(const std::string              &start,
                         const std::string              &target,
                         std::unordered_set<std::string> visited) const
{
  if (start == target) return true;
  if (visited.count(start)) return false;
  visited.insert(start);

  for (const auto &link : this->links)
  {
    if (link.from == start)
    {
      if (is_reachable(link.to, target, visited)) return true;
    }
  }
  return false;
}

// connect two nodes, note that data types are not verified
bool Graph::new_link(const std::string &from,
                     int                port_from,
                     const std::string &to,
                     int                port_to)
{
  Link new_link(from, port_from, to, port_to);

  // Check if the link already exists
  if (std::find(this->links.begin(), this->links.end(), new_link) !=
      this->links.end())
    return false;

  // Get the output data from the source node
  auto from_node_it = this->nodes.find(from);
  if (from_node_it == this->nodes.end())
    throw std::runtime_error("Source node not found: " + from);

  auto to_node_it = this->nodes.find(to);
  if (to_node_it == this->nodes.end())
    throw std::runtime_error("Destination node not found: " + to);

  std::shared_ptr<BaseData> from_data = from_node_it->second->get_output_data(
      port_from);

  // Set the input data on the destination node
  to_node_it->second->set_input_data(from_data, port_to);

  // Add the new link to the list of links
  this->links.push_back(new_link);

  return true;
}

bool Graph::new_link(const std::string &from,
                     const std::string &port_label_from,
                     const std::string &to,
                     const std::string &port_label_to)
{
  auto from_node_it = this->nodes.find(from);
  if (from_node_it == this->nodes.end())
    throw std::runtime_error("Source node not found: " + from);

  auto to_node_it = this->nodes.find(to);
  if (to_node_it == this->nodes.end())
    throw std::runtime_error("Destination node not found: " + to);

  // Check that the 'from' port is an output port
  if (from_node_it->second->get_port_type(port_label_from) != PortType::OUT)
    throw std::invalid_argument("Port '" + port_label_from + "' on node '" +
                                from + "' must be an output port.");

  // Check that the 'to' port is an input port
  if (to_node_it->second->get_port_type(port_label_to) != PortType::IN)
    throw std::invalid_argument("Port '" + port_label_to + "' on node '" + to +
                                "' must be an input port.");

  // Call the existing connect method using the port indices
  return this->new_link(from,
                        from_node_it->second->get_port_index(port_label_from),
                        to,
                        to_node_it->second->get_port_index(port_label_to));
}

void Graph::print()
{
  std::cout << "Graph layout\n";

  // --- Nodes

  std::size_t max_id = 2;
  std::size_t max_label = 5;
  for (const auto &[id, p_node] : this->nodes)
  {
    max_id = std::max(max_id, id.size());
    max_label = std::max(max_label, p_node->get_label().size());
  }

  std::cout << "\n  " << std::left << std::setw(max_id) << "id" << "   "
            << std::left << std::setw(max_label) << "label" << "   "
            << "dirty\n";
  std::cout << "  " << std::string(max_id + max_label + 13, '-') << "\n";

  for (const auto &[id, p_node] : this->nodes)
    std::cout << "  " << std::left << std::setw(max_id) << id << "   "
              << std::left << std::setw(max_label) << p_node->get_label()
              << "   " << std::boolalpha << p_node->is_dirty << "\n";
  std::cout << "\n";

  // --- Links

  for (const auto &link : this->links)
  {
    LinkView view(link, *this->nodes.at(link.from), *this->nodes.at(link.to));
    view.print(/* indent */ 2);
  }
  std::cout << "\n";
}

bool Graph::remove_link(const std::string &from,
                        int                port_from,
                        const std::string &to,
                        int                port_to)
{
  Link link(from, port_from, to, port_to);

  // Check if the link exists
  auto link_it = std::find(this->links.begin(), this->links.end(), link);
  if (link_it == this->links.end()) return false;

  // Ensure the destination node exists before attempting to disconnect
  auto to_node_it = this->nodes.find(to);
  if (to_node_it == this->nodes.end())
    throw std::runtime_error("Destination node not found: " + to);

  // Disconnect nodes by setting the input data to null
  to_node_it->second->set_input_data(nullptr, port_to);

  // Remove the link from the list of links
  this->links.erase(link_it);

  return true;
}

bool Graph::remove_link(const std::string &from,
                        const std::string &port_label_from,
                        const std::string &to,
                        const std::string &port_label_to)
{
  auto from_node_it = this->nodes.find(from);
  auto to_node_it = this->nodes.find(to);
  if (from_node_it == this->nodes.end() || to_node_it == this->nodes.end())
    return false;

  int port_from = from_node_it->second->get_port_index(port_label_from);
  int port_to = to_node_it->second->get_port_index(port_label_to);
  if (port_from == -1 || port_to == -1) return false;

  return this->remove_link(from, port_from, to, port_to);
}

void Graph::remove_node(const std::string &id)
{
  if (this->is_node_id_available(id))
    throw std::runtime_error("Unknown node ID: " + id);

  // Disconnect node by clearing input data on connected nodes
  for (const auto &link : this->links)
    if (link.from == id)
    {
      auto node_it = this->nodes.find(link.to);
      if (node_it != this->nodes.end())
      {
        node_it->second->set_input_data(nullptr, link.port_to);
      }
    }

  // Remove links associated with the node
  this->links.erase(
      std::remove_if(this->links.begin(),
                     this->links.end(),
                     [&id](const Link &link)
                     { return link.from == id || link.to == id; }),
      this->links.end());

  // Remove the node from the graph
  this->nodes.erase(id);
}

std::vector<std::string> Graph::topological_sort(
    const std::vector<std::string> &dirty_node_ids) const
{
  // init
  std::unordered_map<std::string, int> in_degree;
  for (const auto &node_id : dirty_node_ids)
    in_degree[node_id] = 0;

  // node links
  const auto connectivity_up = this->get_connectivity_upstream();
  const auto connectivity_dw = this->get_connectivity_downstream();

  // count number of inputs that are also dirty
  for (const auto &node_id : dirty_node_ids)
    for (const auto &up_id : connectivity_up.at(node_id))
      if (contains(dirty_node_ids, up_id)) in_degree[node_id]++;

  // collect nodes with no dirty dependencies
  std::queue<std::string> ready;
  for (const auto &[node_id, deg] : in_degree)
    if (deg == 0) ready.push(node_id);

  std::vector<std::string> sorted;

  while (!ready.empty())
  {
    std::string node_id = ready.front();
    ready.pop();
    sorted.push_back(node_id);

    for (const auto &dw_id : connectivity_dw.at(node_id))
    {
      if (!in_degree.contains(dw_id)) continue;

      in_degree[dw_id]--;
      if (in_degree[dw_id] == 0) ready.push(dw_id);
    }
  }

  return sorted;
}

void Graph::update()
{
  Logger::log()->trace("Updating graph...");

  // set all nodes to a "dirty" state
  std::vector<std::string> dirty_node_ids = {};

  for (const auto &[nid, _] : this->nodes)
  {
    this->get_node_ref_by_id(nid)->is_dirty = true;
    dirty_node_ids.push_back(nid);
  }

  //
  std::vector<std::string> sorted_id = topological_sort(dirty_node_ids);

  Logger::log()->trace("Graph::update: update queue:");
  for (const auto &s : sorted_id)
    Logger::log()->trace("Graph::update: node id: {}", s);

  for (const auto &nid : sorted_id)
  {
    if (this->cancel_update_callback && this->cancel_update_callback(nid))
      return;

    this->graph_progress_event.notify(nid, sorted_id, true);

    Logger::log()->trace("Updating node: {}({})",
                         this->get_node_ref_by_id(nid)->get_label(),
                         nid);
    this->get_node_ref_by_id(nid)->update();

    this->graph_progress_event.notify(nid, sorted_id, false);
  }

  this->post_update();
}

void Graph::update(const std::vector<std::string> &node_ids)
{
  for (const auto &node_id : node_ids)
  {
    if (this->is_node_id_available(node_id))
      throw std::runtime_error("Graph::update: unknown node id: " + node_id);
  }

  std::vector<std::string> sorted_id = this->get_nodes_to_update(node_ids);

  for (const auto &nid : sorted_id)
  {
    if (this->cancel_update_callback && this->cancel_update_callback(nid))
      return;

    this->graph_progress_event.notify(nid, sorted_id, true);

    Logger::log()->trace("Graph::update: updating node: {}({})",
                         this->get_node_ref_by_id(nid)->get_label(),
                         nid);
    this->get_node_ref_by_id(nid)->is_dirty = true;
    this->get_node_ref_by_id(nid)->update();

    this->graph_progress_event.notify(nid, sorted_id, false);
  }

  this->post_update();
}

void Graph::update(const std::string &node_id)
{
  this->update(std::vector<std::string>{node_id});
}

void Graph::execute_parallel(const std::vector<std::string> &dirty_node_ids,
                             size_t                          num_threads)
{
  if (dirty_node_ids.empty()) return;

  // --- Asynchronous DAG update algorithm:

  // 1. Compute in-degree counts (number of dirty upstream dependencies) for all
  //    dirty nodes in the scheduled update set.
  // 2. Nodes with in-degree 0 (no dirty prerequisites) are enqueued to the
  //    thread pool immediately, executing independent branches in parallel.
  // 3. When a node completes its compute() step, it atomically decrements the
  //    in-degree of each of its direct downstream nodes.
  // 4. When a downstream node's in-degree drops to 0 (all incoming branches
  //    have finished and synchronized), it is scheduled on the thread pool.
  // 5. If cancellation is triggered via cancel_update_callback, remaining
  //    tasks are bypassed and waiting threads are unblocked immediately.

  // Verify the subgraph to update is acyclic before parallel scheduling.
  // This uses the same topological sort size check as has_cycle(), but
  // restricts the cycle detection to the subset of dirty nodes scheduled for
  // this update.
  std::vector<std::string> sorted_id = this->topological_sort(dirty_node_ids);

  if (sorted_id.size() != dirty_node_ids.size())
    throw std::runtime_error(
        "Graph::execute_parallel: cycle detected in nodes to update.");

  // Build in-degree counts and downstream adjacency restricted to
  // dirty_node_ids
  std::unordered_set<std::string> dirty_set(dirty_node_ids.begin(),
                                            dirty_node_ids.end());

  const auto connectivity_up = this->get_connectivity_upstream();
  const auto connectivity_dw = this->get_connectivity_downstream();

  std::unordered_map<std::string, std::shared_ptr<std::atomic<int>>> in_degree;
  std::unordered_map<std::string, std::vector<std::string>>          dirty_dw;

  for (const auto &nid : dirty_node_ids)
  {
    in_degree[nid] = std::make_shared<std::atomic<int>>(0);
    dirty_dw[nid] = {};
  }

  for (const auto &nid : dirty_node_ids)
  {
    for (const auto &up_id : connectivity_up.at(nid))
    {
      if (dirty_set.contains(up_id)) (*in_degree[nid])++;
    }

    for (const auto &dw_id : connectivity_dw.at(nid))
    {
      if (dirty_set.contains(dw_id)) dirty_dw[nid].push_back(dw_id);
    }
  }

  // Coordination primitives
  // Note: sync primitives and state tracking are declared BEFORE the ThreadPool
  // so that worker threads running tasks are joined during ~ThreadPool() BEFORE
  // any of these captured stack synchronization primitives are destroyed.
  std::mutex              sync_mutex;
  std::condition_variable sync_cv;

  std::atomic<bool>  cancelled(false);
  std::atomic<int>   remaining_nodes(static_cast<int>(dirty_node_ids.size()));
  std::exception_ptr captured_error = nullptr;
  std::mutex         error_mutex;

  ThreadPool pool(num_threads);

  // Self-referencing task schedule lambda
  std::function<void(const std::string &)> schedule_node;

  schedule_node = [this,
                   &pool,
                   &in_degree,
                   &dirty_dw,
                   &sync_mutex,
                   &sync_cv,
                   &cancelled,
                   &remaining_nodes,
                   &sorted_id,
                   &captured_error,
                   &error_mutex,
                   &schedule_node](const std::string &nid)
  {
    pool.enqueue(
        [this,
         nid,
         &pool,
         &in_degree,
         &dirty_dw,
         &sync_mutex,
         &sync_cv,
         &cancelled,
         &remaining_nodes,
         &sorted_id,
         &captured_error,
         &error_mutex,
         &schedule_node]()
        {
          if (cancelled.load()) return;

          // Check if execution was cancelled before running this node
          if (this->cancel_update_callback && this->cancel_update_callback(nid))
          {
            cancelled.store(true);

            {
              std::lock_guard<std::mutex> lock(sync_mutex);
              sync_cv.notify_all();
            }

            return;
          }

          try
          {
            this->graph_progress_event.notify(nid, sorted_id, true);

            Logger::log()->trace(
                "Graph::update_parallel: updating node: {}({})",
                this->get_node_ref_by_id(nid)->get_label(),
                nid);

            this->get_node_ref_by_id(nid)->update();

            this->graph_progress_event.notify(nid, sorted_id, false);
          }
          catch (...)
          {
            // Capture first exception thrown by a node and trigger cancellation
            // to unblock the waiting thread and prevent deadlocks
            {
              std::lock_guard<std::mutex> lock(error_mutex);
              if (!captured_error) captured_error = std::current_exception();
            }

            cancelled.store(true);
            --remaining_nodes;

            {
              std::lock_guard<std::mutex> lock(sync_mutex);
              sync_cv.notify_all();
            }

            return;
          }

          if (cancelled.load()) return;

          // Decrement in-degree of downstream dirty nodes
          for (const auto &dw_id : dirty_dw.at(nid))
          {
            int prev_deg = in_degree.at(dw_id)->fetch_sub(1);

            if (prev_deg == 1)
            {
              // All upstream dependencies complete for dw_id; schedule dw_id
              schedule_node(dw_id);
            }
          }

          if (--remaining_nodes == 0)
          {
            std::lock_guard<std::mutex> lock(sync_mutex);
            sync_cv.notify_all();
          }
        });
  };

  // Collect initial nodes with in-degree == 0 before launching any tasks
  // to avoid race conditions with concurrently completing worker threads.
  std::vector<std::string> initial_nodes;

  for (const auto &nid : dirty_node_ids)
  {
    if (in_degree[nid]->load() == 0)
    {
      initial_nodes.push_back(nid);
    }
  }

  // Launch initial nodes
  for (const auto &nid : initial_nodes)
  {
    schedule_node(nid);
  }

  // Wait for all nodes to finish or update to be cancelled
  {
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock,
                 [&remaining_nodes, &cancelled]()
                 { return remaining_nodes.load() == 0 || cancelled.load(); });
  }

  // If a node threw an exception, rethrow it here
  if (captured_error)
  {
    std::rethrow_exception(captured_error);
  }

  if (!cancelled.load())
  {
    this->post_update();
  }
}

std::future<void> Graph::update_async(size_t num_threads)
{
  return std::async(std::launch::async,
                    [this, num_threads]()
                    {
                      Logger::log()->trace(
                          "Updating graph in parallel asynchronously...");

                      std::vector<std::string> dirty_node_ids = {};

                      for (const auto &[nid, _] : this->nodes)
                      {
                        this->get_node_ref_by_id(nid)->is_dirty = true;
                        dirty_node_ids.push_back(nid);
                      }

                      this->execute_parallel(dirty_node_ids, num_threads);
                    });
}

std::future<void> Graph::update_async(const std::vector<std::string> &node_ids,
                                      size_t num_threads)
{
  return std::async(
      std::launch::async,
      [this, node_ids, num_threads]()
      {
        for (const auto &node_id : node_ids)
        {
          if (this->is_node_id_available(node_id))
            throw std::runtime_error("Graph::update_async: unknown node id: " +
                                     node_id);
        }

        std::vector<std::string> dirty_node_ids = this->get_nodes_to_update(
            node_ids);

        for (const auto &nid : dirty_node_ids)
          this->get_node_ref_by_id(nid)->is_dirty = true;

        this->execute_parallel(dirty_node_ids, num_threads);
      });
}

std::future<void> Graph::update_async(const std::string &node_id,
                                      size_t             num_threads)
{
  return this->update_async(std::vector<std::string>{node_id}, num_threads);
}

} // namespace gnode

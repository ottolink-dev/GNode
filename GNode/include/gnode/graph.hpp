/* Copyright (c) 2023 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/**
 * @file graph.hpp
 * @author Otto Link (otto.link.bv@gmail.com)
 * @brief This file defines the Graph class used for managing a collection of
 *        nodes and links in a directed graph. It supports operations for adding
 *        nodes, connecting nodes, exporting the graph, and more.
 * @date 2023-08-07
 *
 * @copyright Copyright (c) 2023
 */

#pragma once
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <unordered_set>

#include "gnode/event.hpp"
#include "gnode/link.hpp"
#include "gnode/node.hpp"
#include "gnode/point.hpp"

typedef unsigned int uint;

namespace gnode
{

/**
 * @brief The Graph class provides methods for manipulating nodes and
 * connections in a directed graph structure.
 */
class Graph
{
public:
  /**
   * @brief Construct a new Graph object.
   */
  Graph() = default;

  /**
   * @brief Construct a new Graph object.
   *
   * @param id Graph ID.
   */
  Graph(const std::string &id) : id(id) {}

  /**
   * @brief Destroy the Graph object.
   */
  virtual ~Graph() = default;

  /**
   * @brief Add a new node to the graph.
   *
   * @param p_node Shared pointer to the node to be added.
   * @param id Optional ID for the node. If empty, an ID will be generated.
   * @return std::string The ID of the added node.
   */
  virtual std::string add_node(const std::shared_ptr<Node> &p_node,
                               const std::string           &id = "");

  /**
   * @brief Add a new node of a specific type to the graph.
   *
   * @tparam U Node type derived from Node class.
   * @tparam Args Arguments for the constructor of the node.
   * @param args Arguments to pass to the node constructor.
   * @return std::string The ID of the added node.
   */
  template <typename U, typename... Args> std::string add_node(Args... args)
  {
    return this->add_node(std::make_shared<U>(args...));
  }

  /**
   * @brief Clear the graph, remove all the nodes and the links.
   */
  void clear();

  /**
   * @brief Compute the layout of the graph using the Sugiyama algorithm.
   *
   * @return std::vector<Point> A vector of points representing the node
   * positions.
   */
  std::vector<Point> compute_graph_layout_sugiyama();

  /**
   * @brief Return the graph ID.
   *
   * @return std::string ID.
   */
  std::string get_id() const { return this->id; };

  /**
   * @brief Connect two nodes in the graph using port indices.
   *
   * @param from ID of the source node.
   * @param port_from Index of the source node's output port.
   * @param to ID of the destination node.
   * @param port_to Index of the destination node's input port.
   * @return true If the connection was successful.
   * @return false If the connection failed.
   */
  bool new_link(const std::string &from,
                int                port_from,
                const std::string &to,
                int                port_to);

  /**
   * @brief Connect two nodes in the graph using port labels.
   *
   * @param from ID of the source node.
   * @param port_label_from Label of the source node's output port.
   * @param to ID of the destination node.
   * @param port_label_to Label of the destination node's input port.
   * @return true If the connection was successful.
   * @return false If the connection failed.
   */
  bool new_link(const std::string &from,
                const std::string &port_label_from,
                const std::string &to,
                const std::string &port_label_to);

  /**
   * @brief Disconnect two nodes in the graph using port indices.
   *
   * @param from ID of the source node.
   * @param port_from Index of the source node's output port.
   * @param to ID of the destination node.
   * @param port_to Index of the destination node's input port.
   * @return true If the disconnection was successful.
   * @return false If the disconnection failed.
   */
  bool remove_link(const std::string &from,
                   int                port_from,
                   const std::string &to,
                   int                port_to);

  /**
   * @brief Disconnect two nodes in the graph using port labels.
   *
   * @param from The label of the source node.
   * @param port_label_from The label of the source node's output port.
   * @param to The label of the destination node.
   * @param port_label_to The label of the destination node's input port.
   * @return true If the disconnection was successful.
   * @return false If the disconnection failed.
   */
  bool remove_link(const std::string &from,
                   const std::string &port_label_from,
                   const std::string &to,
                   const std::string &port_label_to);

  /**
   * @brief Export the graph to a Graphviz DOT file.
   *
   * @param fname Filename of the DOT file.
   * @param graph_label Label for the graph.
   */
  void export_to_graphviz(const std::string &fname = "export.dot",
                          const std::string &graph_label = "graph");

  /**
   * @brief Export the graph to a Mermaid file.
   *
   * @param fname Filename of the Mermaid file.
   * @param graph_label Label for the graph.
   */
  void export_to_mermaid(const std::string &fname = "export.mmd",
                         const std::string &graph_label = "graph");

  /**
   * @brief Get the downstream connectivity of the graph.
   *
   * @return std::map<std::string, std::vector<std::string>> A map of node IDs
   *         and their downstream connections.
   */
  std::map<std::string, std::vector<std::string>> get_connectivity_downstream()
      const;

  /**
   * @brief Returns the downstream nodes connected to a node.
   * @param node_id Identifier of the source node.
   * @return List of downstream node identifiers.
   */
  std::vector<std::string> get_connectivity_downstream(
      const std::string &node_id) const;

  /**
   * @brief Get the upstream connectivity of the graph.
   *
   * @return std::map<std::string, std::vector<std::string>> A map of node IDs
   *         and their upstream connections.
   */
  std::map<std::string, std::vector<std::string>> get_connectivity_upstream()
      const;

  /**
   * @brief Returns the upstream nodes connected to a node.
   * @param node_id Identifier of the destination node.
   * @return List of upstream node identifiers.
   */
  std::vector<std::string> get_connectivity_upstream(
      const std::string &node_id) const;

  /**
   * @brief Get the current count of unique identifiers.
   *
   * @return An available unique identifier.
   * */
  uint get_id_count() const { return this->id_count; }

  uint *get_id_count_ref() { return &this->id_count; }

  /**
   * @brief Get the link storage.
   *
   * @return Links.
   * */
  const std::vector<Link> &get_links() const { return this->links; }

  /**
   * @brief Returns the link views connected to a node.
   *
   * This function collects all links where the specified node is either
   * the source or destination node and converts them into `LinkView`
   * objects enriched with node and port information.
   *
   * Invalid links referencing missing nodes are ignored.
   *
   * @param node_id Identifier of the target node.
   * @return Vector of connected `LinkView` objects.
   */
  std::vector<LinkView> get_link_views(const std::string &node_id) const;

  /**
   * @brief Get a pointer to a node by its ID. Non-throwing, returns nullptr if
   * not found.
   */
  Node *get_node(const std::string &node_id) const
  {
    auto it = nodes.find(node_id);
    return (it != nodes.end()) ? it->second.get() : nullptr;
  }

  /**
   * @brief Get a pointer to a node by its ID.
   *
   * @tparam T Node type, default is Node.
   * @param node_id ID of the node.
   * @return T* Pointer to the node (nullptr is the node is not found).
   * @throws std::runtime_error If casting the node to the
   * specified type fails.
   */
  template <typename T = Node>
  T *get_node_ref_by_id(const std::string &node_id) const
  {
    auto it = nodes.find(node_id);
    if (it == nodes.end()) return nullptr;

    T *ptr = dynamic_cast<T *>(it->second.get());
    if (!ptr)
      throw std::runtime_error("Failed to cast node with ID: " + node_id +
                               " to the specified type.");

    return ptr;
  }

  /**
   * @brief Get a reference to the nodes map.
   *
   * @return Nodes map.
   */
  const std::map<std::string, std::shared_ptr<Node>> &get_nodes()
  {
    return this->nodes;
  }

  /** Get node list for update priority */
  std::vector<std::string> get_nodes_to_update(
      const std::vector<std::string> &node_ids);

  std::vector<std::string> get_nodes_to_update(const std::string &node_id);

  /**
   * @brief Checks whether the graph contains a cycle.
   *
   * This function performs a topological sort on all nodes and compares the
   * result size with the number of nodes in the graph.
   *
   * If the sizes differ, it means that at least one cycle exists in the
   * dependency graph (i.e., some nodes cannot be topologically ordered).
   *
   * @warning This check is purely informational.
   * It is NOT used as a strict execution guard in the current update pipeline,
   * since the graph evaluation system executes nodes in a single-pass
   * topologically ordered schedule and does not rely on reactive or recursive
   * propagation. Therefore, cycles will not cause infinite execution loops,
   * but they may still indicate an invalid or non-deterministic graph
   * structure.
   *
   * @return true if a cycle is detected, false otherwise.
   */
  bool has_cycle() const;

  /**
   * @brief Check if a node ID is available in the graph.
   *
   * @param id Node ID to check.
   * @return true If the ID is available.
   * @return false If the ID is already taken.
   */
  bool is_node_id_available(const std::string &node_id);

  /**
   * @brief Checks whether a target node is reachable from a start node.
   *
   * This function recursively traverses downstream connections to determine
   * whether a path exists between two nodes. It can also be used to detect
   * graph cycles (using start = node_to and target = node_from, i.e.
   * "backwards").
   *
   * @param start Identifier of the starting node.
   * @param target Identifier of the target node.
   * @param visited Set of already visited node identifiers.
   * @return `true` if the target node is reachable, otherwise `false`.
   */
  bool is_reachable(const std::string              &start,
                    const std::string              &target,
                    std::unordered_set<std::string> visited = {}) const;

  /**
   * @brief Method called after the graph update process is completed.
   *
   * This method can be overridden in derived classes for custom post-update
   * behavior.
   */
  virtual void post_update() {}

  /**
   * @brief Print the current graph structure.
   */
  void print();

  /**
   * @brief Remove a node from the graph by its ID.
   *
   * @param id ID of the node to remove.
   */
  virtual void remove_node(const std::string &id);

  /**
   * @brief Set the graph ID.
   *
   * @param new_id ID.
   */
  void set_id(const std::string &new_id) { this->id = new_id; };

  /**
   * @brief Set the current count of unique identifiers.
   *
   * @param new_id An available unique identifier.
   * */
  void set_id_count(uint new_id_count) { this->id_count = new_id_count; }

  /**
   * @brief Event triggered before and after a node is updated.
   *
   * Subscribed callbacks receive:
   * 1. The ID of the node currently being updated.
   * 2. The full sorted list of node IDs scheduled for this update pass.
   * 3. A boolean: true if triggered before the node's update, false if after.
   */
  Event<const std::string &, const std::vector<std::string> &, bool>
      graph_progress_event;

  /**
   * @brief Optional callback to cancel an update, checked before each node.
   *
   * It receives the ID of the node about to be updated. If it returns true, the
   * update stops there: that node and the ones after it are not updated, and
   * post_update() is not called. A node being computed is never interrupted,
   * which makes it usable from an update running on another thread.
   */
  std::function<bool(const std::string &node_id)> cancel_update_callback;

  /** Kahn's algorithm for node sorting for update priority */
  std::vector<std::string> topological_sort(
      const std::vector<std::string> &dirty_node_ids) const;

  /**
   * @brief Mark all nodes as dirty and update the entire graph.
   *
   * This method forces a complete update of the graph, which may be inefficient
   * for large graphs.
   */
  virtual void update();

  /**
   * @brief Update a specific node by its ID.
   *
   * @param id ID of the node to update.
   */
  virtual void update(const std::string &node_id);

  /**
   * @brief Update a specific list of node IDs.
   *
   * @param id IDs of the nodes to update.
   */
  virtual void update(const std::vector<std::string> &node_ids);

  /**
   * @brief Asynchronously update the entire graph in parallel.
   *
   * Independent branches execute concurrently on worker threads.
   * Synchronization is guaranteed for downstream nodes: converging branches
   * synchronize automatically before any shared downstream node begins
   * execution.
   *
   * Returns a std::future that completes when the parallel update finishes.
   *
   * @param num_threads Number of worker threads (0 for hardware concurrency).
   * @return std::future<void> Future tracking update completion.
   */
  virtual std::future<void> update_async(size_t num_threads = 0);

  /**
   * @brief Asynchronously update specific node IDs and their dependencies in
   * parallel.
   *
   * Independent branches execute concurrently on worker threads.
   * Synchronization is guaranteed for downstream nodes: converging branches
   * synchronize automatically before any shared downstream node begins
   * execution.
   *
   * @param node_ids IDs of the nodes to update.
   * @param num_threads Number of worker threads (0 for hardware concurrency).
   * @return std::future<void> Future tracking update completion.
   */
  virtual std::future<void> update_async(
      const std::vector<std::string> &node_ids,
      size_t                          num_threads = 0);

  /**
   * @brief Asynchronously update a specific node and its dependencies in
   * parallel.
   *
   * Independent branches execute concurrently on worker threads.
   * Synchronization is guaranteed for downstream nodes: converging branches
   * synchronize automatically before any shared downstream node begins
   * execution.
   *
   * @param node_id ID of the node to update.
   * @param num_threads Number of worker threads (0 for hardware concurrency).
   * @return std::future<void> Future tracking update completion.
   */
  virtual std::future<void> update_async(const std::string &node_id,
                                         size_t             num_threads = 0);

protected:
  /**
   * @brief Execute a set of dirty nodes concurrently using task-based
   * dependency counting.
   *
   * @param dirty_node_ids Set of node IDs scheduled for this update.
   * @param num_threads Number of worker threads.
   */
  void execute_parallel(const std::vector<std::string> &dirty_node_ids,
                        size_t                          num_threads);
  /**
   * @brief A map of node IDs to shared pointers of Node objects.
   */
  std::map<std::string, std::shared_ptr<Node>> nodes;

  /**
   * @brief A list of links between nodes in the graph.
   */
  std::vector<Link> links;

private:
  /**
   * @brief Keep track of unique identifiers.
   */
  uint id_count = 0;

  /**
   * @brief Graph id
   */
  std::string id = "";
};

// helper

template <typename T> bool contains(const std::vector<T> &vec, const T &value)
{
  return std::find(vec.begin(), vec.end(), value) != vec.end();
}

} // namespace gnode

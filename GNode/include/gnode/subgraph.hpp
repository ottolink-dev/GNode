/* Copyright (c) 2023 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/**
 * @file subgraph.hpp
 * @author Otto Link (otto.link.bv@gmail.com)
 * @brief Defines the SubGraphNode, NodeIn, and NodeOut classes for hierarchical
 *        graph compositions (Graph of Graphs).
 * @date 2026-10-24
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gnode/graph.hpp"
#include "gnode/node.hpp"
#include "gnode/port.hpp"

namespace gnode
{

// ============================================================================
// Boundary Node Interfaces
// ============================================================================

/**
 * @brief Base interface for boundary input nodes placed inside a subgraph.
 */
class ISubGraphInputNode
{
public:
  virtual ~ISubGraphInputNode() = default;

  virtual void pull_from_parent(Node              *parent_node,
                                const std::string &port_label) = 0;

  virtual std::string get_slot_name() const = 0;
};

/**
 * @brief Base interface for boundary output nodes placed inside a subgraph.
 */
class ISubGraphOutputNode
{
public:
  virtual ~ISubGraphOutputNode() = default;

  virtual void push_to_parent(Node              *parent_node,
                              const std::string &port_label) = 0;

  virtual std::string get_slot_name() const = 0;
};

// ============================================================================
// Boundary Node Implementations
// ============================================================================

/**
 * @brief Represents an input boundary slot inside a subgraph.
 *
 * Inside the child graph, NodeIn acts as a data source producing an OUT port.
 * When the parent SubGraphNode evaluates, it forwards data into this node.
 */
template <typename T> class NodeIn : public Node, public ISubGraphInputNode
{
public:
  NodeIn(const std::string &slot_name) : Node("NodeIn"), slot_name(slot_name)
  {
    this->add_port<T>(PortType::OUT, "value");
  }

  void compute() override {}

  std::string get_slot_name() const override { return this->slot_name; }

  void pull_from_parent(Node              *parent_node,
                        const std::string &port_label) override
  {
    T *parent_val = parent_node->get_value_ref<T>(port_label);
    T *inner_val = this->get_value_ref<T>("value");

    if (parent_val && inner_val)
    {
      if (*inner_val != *parent_val)
      {
        *inner_val = *parent_val;
        this->is_dirty = true;
      }
    }
  }

private:
  std::string slot_name;
};

/**
 * @brief Represents an output boundary slot inside a subgraph.
 *
 * Inside the child graph, NodeOut acts as a data sink receiving an IN port.
 * When the child graph finishes evaluating, the parent SubGraphNode reads from
 * this node and pushes results to its external output ports.
 */
template <typename T> class NodeOut : public Node, public ISubGraphOutputNode
{
public:
  NodeOut(const std::string &slot_name) : Node("NodeOut"), slot_name(slot_name)
  {
    this->add_port<T>(PortType::IN, "value");
  }

  void compute() override {}

  std::string get_slot_name() const override { return this->slot_name; }

  void push_to_parent(Node *parent_node, const std::string &port_label) override
  {
    T *inner_val = this->get_value_ref<T>("value");
    T *parent_val = parent_node->get_value_ref<T>(port_label);

    if (inner_val && parent_val)
    {
      *parent_val = *inner_val;
    }
  }

private:
  std::string slot_name;
};

// ============================================================================
// SubGraphNode
// ============================================================================

/**
 * @brief Composite Node that encapsulates an entire child Graph.
 *
 * Enables hierarchical graph composition (Graph of Graphs). Boundary inputs
 * (NodeIn) and boundary outputs (NodeOut) in the child graph can be registered
 * as external ports of this node.
 */
class SubGraphNode : public Node
{
public:
  SubGraphNode(const std::string &label = "SubGraphNode")
      : Node(label), subgraph(nullptr)
  {
  }

  SubGraphNode(const std::string &label, std::shared_ptr<Graph> subgraph)
      : Node(label), subgraph(subgraph)
  {
  }

  virtual ~SubGraphNode() = default;

  /**
   * @brief Manually exposes a NodeIn as an external input port on this node.
   */
  template <typename T>
  void expose_input(const std::shared_ptr<NodeIn<T>> &node_in)
  {
    const std::string &name = node_in->get_slot_name();
    this->add_port<T>(PortType::IN, name);
    this->inputs.push_back({name, node_in});
  }

  /**
   * @brief Manually exposes a NodeOut as an external output port on this node.
   */
  template <typename T>
  void expose_output(const std::shared_ptr<NodeOut<T>> &node_out)
  {
    const std::string &name = node_out->get_slot_name();
    this->add_port<T>(PortType::OUT, name);
    this->outputs.push_back({name, node_out});
  }

  /**
   * @brief Automatically inspects the child graph and registers all NodeIn and
   * NodeOut nodes as ports.
   */
  void auto_bind_ports();

  /**
   * @brief Computes the subgraph node by transferring inputs, updating the
   * child graph, and transferring outputs.
   */
  void compute() override;

  /**
   * @brief Set or replace the encapsulated child graph.
   */
  void set_subgraph(std::shared_ptr<Graph> new_subgraph)
  {
    this->subgraph = new_subgraph;
  }

  /**
   * @brief Get the encapsulated child graph.
   */
  std::shared_ptr<Graph> get_subgraph() const { return this->subgraph; }

private:
  struct InputBinding
  {
    std::string                         name;
    std::shared_ptr<ISubGraphInputNode> node;
  };

  struct OutputBinding
  {
    std::string                          name;
    std::shared_ptr<ISubGraphOutputNode> node;
  };

  std::shared_ptr<Graph>     subgraph;
  std::vector<InputBinding>  inputs;
  std::vector<OutputBinding> outputs;
};

} // namespace gnode

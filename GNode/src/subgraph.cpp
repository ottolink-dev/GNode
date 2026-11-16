/* Copyright (c) 2023 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

#include "gnode/subgraph.hpp"
#include "gnode/logger.hpp"

namespace gnode
{

void SubGraphNode::auto_bind_ports()
{
  if (!this->subgraph) return;

  for (const auto &[_, node_ptr] : this->subgraph->get_nodes())
  {
    auto input_node = std::dynamic_pointer_cast<ISubGraphInputNode>(node_ptr);
    if (input_node)
    {
      std::string slot_name = input_node->get_slot_name();
      auto       *as_node = dynamic_cast<Node *>(input_node.get());
      if (as_node && as_node->get_nports() > 0)
      {
        // Check if port already exists
        if (!this->has_port(slot_name))
        {
          input_node->register_on_parent(this, slot_name);
          this->inputs.push_back({slot_name, input_node});
        }
      }
    }

    auto output_node = std::dynamic_pointer_cast<ISubGraphOutputNode>(node_ptr);
    if (output_node)
    {
      std::string slot_name = output_node->get_slot_name();
      auto       *as_node = dynamic_cast<Node *>(output_node.get());
      if (as_node && as_node->get_nports() > 0)
      {
        if (!this->has_port(slot_name))
        {
          output_node->register_on_parent(this, slot_name);
          this->outputs.push_back({slot_name, output_node});
        }
      }
    }
  }
}

void SubGraphNode::compute()
{
  // 1. Pull values from parent graph into child NodeIn boundary nodes
  for (auto &[port_name, in_node] : this->inputs)
  {
    in_node->pull_from_parent(this, port_name);
  }

  // 2. Evaluate child graph
  if (this->subgraph)
  {
    this->subgraph->update();
  }

  // 3. Push values from child NodeOut boundary nodes to parent graph
  for (auto &[port_name, out_node] : this->outputs)
  {
    out_node->push_to_parent(this, port_name);
  }
}

void SubGraphNode::on_pre_async_compute()
{
  if (!this->subgraph) return;

  for (const auto &[_, node_ptr] : this->subgraph->get_nodes())
  {
    if (node_ptr) node_ptr->on_pre_async_compute();
  }
}

void SubGraphNode::on_async_compute_cancelled()
{
  if (!this->subgraph) return;

  for (const auto &[_, node_ptr] : this->subgraph->get_nodes())
  {
    if (node_ptr) node_ptr->on_async_compute_cancelled();
  }
}

void SubGraphNode::set_subgraph(std::shared_ptr<Graph> new_subgraph)
{
  this->inputs.clear();
  this->outputs.clear();
  this->subgraph = new_subgraph;
}

} // namespace gnode

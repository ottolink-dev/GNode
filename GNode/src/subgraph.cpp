/* Copyright (c) 2023 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

#include "gnode/subgraph.hpp"
#include "gnode/logger.hpp"

#include <unordered_set>

namespace gnode
{

void SubGraphNode::auto_bind_ports()
{
  if (!this->subgraph) return;

  // 1. Snapshot incident links in parent graph by port label before mutating
  // ports
  struct LinkSnapshot
  {
    std::string from;
    std::string port_label_from;
    std::string to;
    std::string port_label_to;
  };

  std::vector<LinkSnapshot> incident_links;
  Graph                    *parent_graph = this->get_p_graph();

  if (parent_graph)
  {
    const std::string my_id = this->get_id();
    for (const auto &link : parent_graph->get_links())
    {
      if (link.from == my_id || link.to == my_id)
      {
        Node *p_from = parent_graph->get_node(link.from);
        Node *p_to = parent_graph->get_node(link.to);
        if (p_from && p_to)
        {
          incident_links.push_back({link.from,
                                    p_from->get_port_label(link.port_from),
                                    link.to,
                                    p_to->get_port_label(link.port_to)});
        }
      }
    }

    for (const auto &snap : incident_links)
      parent_graph->remove_link(snap.from,
                                snap.port_label_from,
                                snap.to,
                                snap.port_label_to);
  }

  std::unordered_set<std::string> active_inputs;
  std::unordered_set<std::string> active_outputs;

  for (const auto &[_, node_ptr] : this->subgraph->get_nodes())
  {
    auto input_node = std::dynamic_pointer_cast<ISubGraphInputNode>(node_ptr);
    if (input_node)
    {
      std::string slot_name = input_node->get_slot_name();
      auto       *as_node = dynamic_cast<Node *>(input_node.get());
      if (as_node && as_node->get_nports() > 0)
      {
        active_inputs.insert(slot_name);

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
        active_outputs.insert(slot_name);

        if (!this->has_port(slot_name))
        {
          output_node->register_on_parent(this, slot_name);
          this->outputs.push_back({slot_name, output_node});
        }
      }
    }
  }

  std::erase_if(this->inputs,
                [this, &active_inputs](const InputBinding &binding)
                {
                  if (!active_inputs.contains(binding.name))
                  {
                    this->remove_port(binding.name);
                    return true;
                  }
                  return false;
                });

  std::erase_if(this->outputs,
                [this, &active_outputs](const OutputBinding &binding)
                {
                  if (!active_outputs.contains(binding.name))
                  {
                    this->remove_port(binding.name);
                    return true;
                  }
                  return false;
                });

  // 3. Reconcile snapshotted links with updated ports
  if (parent_graph)
  {
    for (const auto &snap : incident_links)
    {
      Node *p_from = parent_graph->get_node(snap.from);
      Node *p_to = parent_graph->get_node(snap.to);
      if (!p_from || !p_to) continue;

      const int out_idx = p_from->get_port_index(snap.port_label_from);
      const int in_idx = p_to->get_port_index(snap.port_label_to);

      bool can_rebind = (out_idx >= 0 && in_idx >= 0 &&
                         p_from->get_port_type(snap.port_label_from) ==
                             PortType::OUT &&
                         p_to->get_port_type(snap.port_label_to) ==
                             PortType::IN &&
                         p_from->get_data_type(out_idx) ==
                             p_to->get_data_type(in_idx));

      if (can_rebind)
        parent_graph->new_link(snap.from, out_idx, snap.to, in_idx);
      else
        p_to->is_dirty = true;
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
  this->sync_outputs();
}

void SubGraphNode::sync_outputs()
{
  for (auto &[port_name, out_node] : this->outputs)
  {
    if (out_node) out_node->push_to_parent(this, port_name);
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

# Architecture

# Introduction

**GNode** is a lightweight and extensible C++ framework for building
node-based graphs. It allows you to define nodes, connect them through
input/output ports, and process data through a directed graph
structure. GNode is designed to be:

- **Modular**: you create only the nodes you need
- **Type-safe**: strong typing of ports and attributes  
- **Extensible**: supports custom types as long as they are copyable or movable  

A graph in GNode is composed of:

- **Nodes**: units of logic or data  
- **Ports**: typed inputs/outputs on nodes  
- **Links**: connections between ports  
- **Graph (supervisor)**: manager for node creation, registration, and update  

# Installation

See the README in the GitHub repository:

https://github.com/otto-link/GNode

# Usage Example

```cpp
#include "gnode.hpp"
#include <iostream>

// create a class to show that complex objects can be handled
struct Vec2
{
  float x;
  float y;
  Vec2() = default;
  Vec2(float x, float y) : x(x), y(y) {}
};

// ----------------------------------------
// Nodes definition, all derivates from gnode::Node
// ----------------------------------------

// holds a float value
class Value : public gnode::Node
{
public:
  Value() : gnode::Node("Value")
  {
    // declare an output port of type float
    this->add_port<float>(gnode::PortType::OUT, "value");
  }

  Value(float value) : gnode::Node("Value")
  {
    this->add_port<float>(gnode::PortType::OUT, "value");
    // directly set initial value of the port
    this->set_value<float>("value", value);
  }

  // leaf node: nothing to compute, only stores a value
  void compute() {}
};

// holds a Vec2 value
class ValueVec : public gnode::Node
{
public:
  ValueVec() : gnode::Node("ValueVec")
  {
    // declare an output port containing Vec2 objects
    this->add_port<Vec2>(gnode::PortType::OUT, "value");
  }

  ValueVec(float x, float y) : gnode::Node("ValueVec")
  {
    // option 1: pass constructor arguments for Vec2
    this->add_port<Vec2>(gnode::PortType::OUT, "value", x, y);

    // option 2: set the value manually
    // this->add_port<Vec2>(gnode::PortType::OUT, "value");
    // this->set_value<Vec2>("value", Vec2(x, y));
  }

  void compute() {}
};

// add two floats
class Add : public gnode::Node
{
public:
  Add() : gnode::Node("Add")
  {
    // two float inputs
    this->add_port<float>(gnode::PortType::IN, "a");
    this->add_port<float>(gnode::PortType::IN, "b");

    // one float output
    this->add_port<float>(gnode::PortType::OUT, "a + b");
  }

  void compute()
  {
    // access input/output values by reference
    float *p_in1 = this->get_value_ref<float>("a");
    float *p_in2 = this->get_value_ref<float>("b");
    float *p_out = this->get_value_ref<float>("a + b");

    // compute only if both inputs are connected/valid
    if (p_in1 && p_in2) *p_out = *p_in1 + *p_in2;
  }
};

// sum Vec2 components and returns a float
class SumVec : public gnode::Node
{
public:
  SumVec() : gnode::Node("SumVec")
  {
    // Vec2 input
    this->add_port<Vec2>(gnode::PortType::IN, "vec");
    // float output
    this->add_port<float>(gnode::PortType::OUT, "sum");
  }

  void compute()
  {
    Vec2  *p_vec = this->get_value_ref<Vec2>("vec");
    float *p_sum = this->get_value_ref<float>("sum");

    // sum only if input exists
    if (p_vec) *p_sum = p_vec->x + p_vec->y;
  }
};

// print a float
class Print : public gnode::Node
{
public:
  Print() : gnode::Node("Print")
  {
    this->add_port<float>(gnode::PortType::IN, "in");
  }

  void compute()
  {
    float *p_in = this->get_value_ref<float>("in");

    if (p_in) std::cout << "PRINTING: " << *p_in << "\n";
  }
};

int main()
{
  gnode::Graph g;

  // NB - node constructor parameters are pass through when adding a node
  // Create a Vec2 node with initial value (1, 2)
  auto id_value_vec = g.add_node<ValueVec>(1.f, 2.f);
  auto id_sum_vec = g.add_node<SumVec>();

  // sum vector components
  g.new_link(id_value_vec, "value", id_sum_vec, "vec");

  // add the former sum to a float
  auto id_value1 = g.add_node<Value>(3.f);
  auto id_add1 = g.add_node<Add>();
  g.new_link(id_sum_vec, "sum", id_add1, "a");
  g.new_link(id_value1, "value", id_add1, "b");

  // print this intermediate value
  auto id_print1 = g.add_node<Print>();
  g.new_link(id_add1, "a + b", id_print1, "in");

  // add the former sum to another float...
  auto id_value2 = g.add_node<Value>(4.f);
  auto id_add2 = g.add_node<Add>();
  g.new_link(id_add1, "a + b", id_add2, "a");
  g.new_link(id_value2, "value", id_add2, "b");

  // and print the results
  auto id_print2 = g.add_node<Print>();
  g.new_link(id_add2, "a + b", id_print2, "in");

  // once the graph is defined, perform an overall update to ensure a
  // clean initial graph state (topological greedy update)
  std::cout << "\nOVERALL UPDATE\n";
  g.update();

  // --- change one node state = only update from this node
  // --- propagate changes only to nodes that depend on it
  std::cout << "\nNODE UPDATE\n";
  g.get_node_ref_by_id<Value>(id_value1)->set_value<float>("value", 10.f);

  // incremental update from id_value1
  g.update(id_value1);

  // export for debugging/visualization
  // dot export.dot -Tsvg > output.svg
  g.export_to_graphviz();
  g.export_to_mermaid();

  return 0;
}
```

# Architecture

## Overview

- **Nodes** hold logic and expose typed **input/output ports**
- **Ports** transport typed **data objects**
- **Links** connect an output port of one node to an input port of another
- **Graphs** manage nodes, links, evaluation order, and data propagation

The library is minimal, dependency-free except for `<memory>`, `<vector>`, and optional `spdlog`.

# Key Components

## BaseData and Data<T> (in data.hpp)

## Responsibility

Represent typed, mutable values transported through ports.

### Structure

* `BaseData`

  * Abstract interface:

    * `get_type()`
* `Data<T>`

  * Stores a value of type `T`
  * Provides:

    * `get_value_ref()`
    * Type-safe clone
    * Access via `T*` for internal generic port-level handling
    * Value set by the `GNode::Node` class: `GNode::Node::set_value`

## Role in architecture

`Data<T>` is the foundation of type-safety at the port level. Every
output port owns its `Data<T>`, and input ports bind to the same
instance when linked.

## Ports - InputPort and OutputPort (in port.hpp)

Ports connect nodes.

### Port

* Knows:

  * Port name
  * Node ID
  * Port ID inside the node

### OutputPort<T>

* Owns a `shared_ptr<Data<T>>`
* Always produces data
* Can be connected to any number of `InputPort`

### InputPort<T>

* Does **not own data**
* Holds a pointer to a `Data<T>` owned by an `OutputPort`
* If unconnected, the data pointer is `nullptr`
* Can be disconnect to only one `OutputPort`

### Role

Ports are the **typed interface** between nodes. `InputPorts` never
store data, they alias data from the upstream `OutputPort`.

## Link (in link.hpp)

A **Link** is a simple POD struct:

* `std::string from_node_id`
* `std::string to_node_id`
* `int from_port_index`
* `int to_port_index`

### Purpose

Represents a connection between two ports in a graph, used by `Graph`
to physically link port objects.

## Node (in node.hpp)

The most important unit.

### Responsibilities

* Represent a computational unit
* Own ports
* Implement specific logic
* Expose:

  * label
  * id
  * input ports list
  * output ports list

### Important API

* `add_port<T>(port_type, label, ...)`
* `compute()` — pure virtual: each node defines its logic
* `T *get_value_ref<T>(port_label)`

### Design Pattern

**Nodes use templates to declare typed ports**, but evaluation remains virtual (dynamic).

## Graph (in graph.hpp)

The orchestrator.

### Responsibilities

* Store nodes (`std::map<std::string, shared_ptr<Node>>`)
* Store links
* Resolve and connect ports
* Provide update/evaluation sequences

### Important methods

* `add_node<...>(...)`: add a new node of a specific type to the graph (template-based)
* `new_link(from_id, out_port_idx, to_id, in_port_idx)` - connections from output to input it "one to many" 
* `update()` - mark all nodes as dirty and update the entire graph
* `update(node_id)` - update a specific node by its ID and propagate modifications to other nodes
* `remove_link(...)`
* `remove_node(id)`
* `T* get_node_ref_by_id(id)`

## SubGraph and Hierarchical Graphs (in subgraph.hpp)

GNode supports hierarchical graph composition (Graph of Graphs). An entire `Graph` can be nested inside another graph as an atomic node.

### Components

* `NodeIn<T>`: Boundary input node placed inside the child graph. Exposes an internal `OUT` port to downstream inner nodes. When the parent graph computes the subgraph, values from the parent node are pulled into this node.
* `NodeOut<T>`: Boundary output node placed inside the child graph. Exposes an internal `IN` port to receive computed results from upstream inner nodes.
* `SubGraphNode`: A `Node` that encapsulates a `std::shared_ptr<Graph>`.
  * `expose_input<T>(node_in)`: Maps a child `NodeIn<T>` to an external input port on the parent node.
  * `expose_output<T>(node_out)`: Maps a child `NodeOut<T>` to an external output port on the parent node.
  * `auto_bind_ports()`: Automatically inspects the encapsulated graph to register boundary inputs and outputs.
  * `compute()`: Pulls input values from parent ports into child `NodeIn` nodes, executes `subgraph->update()`, and pushes results from child `NodeOut` nodes to parent output ports.

### Benefits

* Encapsulation and modularity: subgraphs can be reused, nested at arbitrary depths ($N$-level nesting), and tested independently.
* Seamless integration: subgraphs participate in standard topological sorting and dependency resolution of the parent graph without requiring special inter-graph edge mechanics.

## Evaluation Strategy

The graph **does implement a dependency solver and topological
sorting**. The user can decide not to use the provided `update()`
methods and define its own scheduling policy.

## Architecture Diagram

```
       +--------------+     link     +--------------+
       |   Node A     |------------>|    Node B    |
       |              |             |              |
       |  [out:T]-----+             +-----[in:T]   |
       +--------------+             +--------------+
                ^
                |
          Data<T> stored in OutputPort
                |
          InputPort references the same Data<T>
```

## Dataflow Model

* A node computes some output in `compute()`
* All connected `InputPorts` see the same `Data<T>` pointer
* Downstream nodes read inputs during their own `compute()`
* In the `compute()` node method, data are accessed by reference:

```cpp
void compute()
{
  // access input/output values by reference
  float *p_in1 = this->get_value_ref<float>("a");
  float *p_in2 = this->get_value_ref<float>("b");
  float *p_out = this->get_value_ref<float>("a + b");

  // compute only if both inputs are connected/valid
  if (p_in1 && p_in2) *p_out = *p_in1 + *p_in2;
}
```

# Repository

- GitHub: [https://github.com/otto-link/GNode](https://github.com/otto-link/GNode)
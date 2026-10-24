#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "gnode.hpp"

// ============================================================================
// Basic computation nodes for testing
// ============================================================================

class Value : public gnode::Node
{
public:
  Value(float val) : gnode::Node("Value")
  {
    this->add_port<float>(gnode::PortType::OUT, "value");
    this->set_value<float>("value", val);
  }

  void compute() override {}
};

class Add : public gnode::Node
{
public:
  Add() : gnode::Node("Add")
  {
    this->add_port<float>(gnode::PortType::IN, "a");
    this->add_port<float>(gnode::PortType::IN, "b");
    this->add_port<float>(gnode::PortType::OUT, "sum");
  }

  void compute() override
  {
    float *p_a   = this->get_value_ref<float>("a");
    float *p_b   = this->get_value_ref<float>("b");
    float *p_out = this->get_value_ref<float>("sum");

    if (p_a && p_b && p_out)
    {
      *p_out = *p_a + *p_b;
    }
  }
};

class Multiply : public gnode::Node
{
public:
  Multiply(float factor) : gnode::Node("Multiply"), factor(factor)
  {
    this->add_port<float>(gnode::PortType::IN, "in");
    this->add_port<float>(gnode::PortType::OUT, "out");
  }

  void compute() override
  {
    float *p_in  = this->get_value_ref<float>("in");
    float *p_out = this->get_value_ref<float>("out");

    if (p_in && p_out)
    {
      *p_out = (*p_in) * this->factor;
    }
  }

private:
  float factor;
};

// ============================================================================
// Main test
// ============================================================================

int main()
{
  std::cout << "Running test_subgraph using gnode::SubGraph..." << std::endl;

  // 1. Build an inner subgraph computing: (x + y) * 2.0
  auto inner_graph = std::make_shared<gnode::Graph>("InnerMathGraph");

  auto in_x     = std::make_shared<gnode::NodeIn<float>>("x");
  auto in_y     = std::make_shared<gnode::NodeIn<float>>("y");
  auto out_res  = std::make_shared<gnode::NodeOut<float>>("result");
  auto add_node = std::make_shared<Add>();
  auto mul_node = std::make_shared<Multiply>(2.0f);

  inner_graph->add_node(in_x, "in_x");
  inner_graph->add_node(in_y, "in_y");
  inner_graph->add_node(add_node, "add");
  inner_graph->add_node(mul_node, "mul");
  inner_graph->add_node(out_res, "out_res");

  inner_graph->new_link("in_x", "value", "add", "a");
  inner_graph->new_link("in_y", "value", "add", "b");
  inner_graph->new_link("add", "sum", "mul", "in");
  inner_graph->new_link("mul", "out", "out_res", "value");

  // 2. Wrap inner graph in gnode::SubGraphNode
  auto subgraph =
      std::make_shared<gnode::SubGraphNode>("MathSubGraph", inner_graph);
  subgraph->expose_input(in_x);
  subgraph->expose_input(in_y);
  subgraph->expose_output(out_res);

  // 3. Build root graph
  gnode::Graph root_graph("RootGraph");

  auto val_1     = std::make_shared<Value>(3.0f);
  auto val_2     = std::make_shared<Value>(4.0f);
  auto add_after = std::make_shared<Add>();
  auto val_extra = std::make_shared<Value>(10.0f);

  root_graph.add_node(val_1, "val_1");
  root_graph.add_node(val_2, "val_2");
  root_graph.add_node(subgraph, "subgraph");
  root_graph.add_node(val_extra, "val_extra");
  root_graph.add_node(add_after, "add_after");

  // Wire root graph -> subgraph -> downstream
  // (3 + 4) * 2 = 14; 14 + 10 = 24
  root_graph.new_link("val_1", "value", "subgraph", "x");
  root_graph.new_link("val_2", "value", "subgraph", "y");
  root_graph.new_link("subgraph", "result", "add_after", "a");
  root_graph.new_link("val_extra", "value", "add_after", "b");

  // 4. Update root graph
  root_graph.update();

  float *final_val = add_after->get_value_ref<float>("sum");
  assert(final_val != nullptr);
  std::cout << "Computed result in parent graph: " << *final_val << std::endl;

  assert(*final_val == 24.0f);
  std::cout << "Assertion passed: 24.0 == " << *final_val << std::endl;

  // 5. Test dynamic input change and incremental update
  val_1->set_value<float>("value", 10.0f);
  // (10 + 4) * 2 = 28; 28 + 10 = 38
  root_graph.update();

  float *updated_val = add_after->get_value_ref<float>("sum");
  assert(updated_val != nullptr);
  std::cout << "Updated result after changing val_1: " << *updated_val
            << std::endl;
  assert(*updated_val == 38.0f);

  std::cout << "All subgraph tests completed successfully!" << std::endl;
  return 0;
}

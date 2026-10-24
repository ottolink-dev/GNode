#include <gtest/gtest.h>

#include "gnode.hpp"

namespace
{

class ValueNode : public gnode::Node
{
public:
  ValueNode(float val = 0.0f) : gnode::Node("Value")
  {
    this->add_port<float>(gnode::PortType::OUT, "value");
    this->set_value<float>("value", val);
  }

  void compute() override {}
};

class AddNode : public gnode::Node
{
public:
  AddNode() : gnode::Node("Add")
  {
    this->add_port<float>(gnode::PortType::IN, "a");
    this->add_port<float>(gnode::PortType::IN, "b");
    this->add_port<float>(gnode::PortType::OUT, "sum");
  }

  void compute() override
  {
    float *p_a = this->get_value_ref<float>("a");
    float *p_b = this->get_value_ref<float>("b");
    float *p_out = this->get_value_ref<float>("sum");

    if (p_a && p_b && p_out)
    {
      *p_out = *p_a + *p_b;
    }
  }
};

class MultiplyNode : public gnode::Node
{
public:
  MultiplyNode(float factor = 1.0f) : gnode::Node("Multiply"), factor(factor)
  {
    this->add_port<float>(gnode::PortType::IN, "in");
    this->add_port<float>(gnode::PortType::OUT, "out");
  }

  void compute() override
  {
    float *p_in = this->get_value_ref<float>("in");
    float *p_out = this->get_value_ref<float>("out");

    if (p_in && p_out)
    {
      *p_out = (*p_in) * this->factor;
    }
  }

private:
  float factor;
};

} // namespace

TEST(SubGraphTest, BasicHierarchicalEvaluation)
{
  // Build child graph computing: (x + y) * 3
  auto child_graph = std::make_shared<gnode::Graph>("ChildMath");

  auto in_x = std::make_shared<gnode::NodeIn<float>>("x");
  auto in_y = std::make_shared<gnode::NodeIn<float>>("y");
  auto out_res = std::make_shared<gnode::NodeOut<float>>("result");
  auto add_node = std::make_shared<AddNode>();
  auto mul_node = std::make_shared<MultiplyNode>(3.0f);

  child_graph->add_node(in_x, "in_x");
  child_graph->add_node(in_y, "in_y");
  child_graph->add_node(add_node, "add");
  child_graph->add_node(mul_node, "mul");
  child_graph->add_node(out_res, "out_res");

  EXPECT_TRUE(child_graph->new_link("in_x", "value", "add", "a"));
  EXPECT_TRUE(child_graph->new_link("in_y", "value", "add", "b"));
  EXPECT_TRUE(child_graph->new_link("add", "sum", "mul", "in"));
  EXPECT_TRUE(child_graph->new_link("mul", "out", "out_res", "value"));

  // Wrap in SubGraphNode
  auto subgraph_node = std::make_shared<gnode::SubGraphNode>("MathSubGraph",
                                                             child_graph);
  subgraph_node->expose_input(in_x);
  subgraph_node->expose_input(in_y);
  subgraph_node->expose_output(out_res);

  // Parent Graph
  gnode::Graph root_graph("RootGraph");

  auto val_1 = std::make_shared<ValueNode>(2.0f);
  auto val_2 = std::make_shared<ValueNode>(5.0f);
  auto add_after = std::make_shared<AddNode>();
  auto val_after = std::make_shared<ValueNode>(1.0f);

  root_graph.add_node(val_1, "val_1");
  root_graph.add_node(val_2, "val_2");
  root_graph.add_node(subgraph_node, "subgraph");
  root_graph.add_node(val_after, "val_after");
  root_graph.add_node(add_after, "add_after");

  // Wire: (2 + 5) * 3 = 21, then 21 + 1 = 22
  EXPECT_TRUE(root_graph.new_link("val_1", "value", "subgraph", "x"));
  EXPECT_TRUE(root_graph.new_link("val_2", "value", "subgraph", "y"));
  EXPECT_TRUE(root_graph.new_link("subgraph", "result", "add_after", "a"));
  EXPECT_TRUE(root_graph.new_link("val_after", "value", "add_after", "b"));

  root_graph.update();

  float *final_val = add_after->get_value_ref<float>("sum");
  ASSERT_NE(final_val, nullptr);
  EXPECT_FLOAT_EQ(*final_val, 22.0f);

  // Dynamic input change and re-evaluation: val_1 becomes 10 -> (10 + 5) * 3 =
  // 45; 45 + 1 = 46
  val_1->set_value<float>("value", 10.0f);
  root_graph.update();

  float *updated_val = add_after->get_value_ref<float>("sum");
  ASSERT_NE(updated_val, nullptr);
  EXPECT_FLOAT_EQ(*updated_val, 46.0f);
}

TEST(SubGraphTest, NestedSubGraphs)
{
  // Deepest level: double input
  auto inner_inner_graph = std::make_shared<gnode::Graph>("InnerInner");
  auto level2_in = std::make_shared<gnode::NodeIn<float>>("in_val");
  auto level2_mul = std::make_shared<MultiplyNode>(2.0f);
  auto level2_out = std::make_shared<gnode::NodeOut<float>>("out_val");

  inner_inner_graph->add_node(level2_in, "l2_in");
  inner_inner_graph->add_node(level2_mul, "l2_mul");
  inner_inner_graph->add_node(level2_out, "l2_out");
  inner_inner_graph->new_link("l2_in", "value", "l2_mul", "in");
  inner_inner_graph->new_link("l2_mul", "out", "l2_out", "value");

  auto l2_subgraph_node = std::make_shared<gnode::SubGraphNode>(
      "L2SubGraph",
      inner_inner_graph);
  l2_subgraph_node->expose_input(level2_in);
  l2_subgraph_node->expose_output(level2_out);

  // Mid level: add 5, then call L2
  auto mid_graph = std::make_shared<gnode::Graph>("MidGraph");
  auto mid_in = std::make_shared<gnode::NodeIn<float>>("mid_in");
  auto mid_add = std::make_shared<AddNode>();
  auto mid_const = std::make_shared<ValueNode>(5.0f);
  auto mid_out = std::make_shared<gnode::NodeOut<float>>("mid_out");

  mid_graph->add_node(mid_in, "m_in");
  mid_graph->add_node(mid_const, "m_const");
  mid_graph->add_node(mid_add, "m_add");
  mid_graph->add_node(l2_subgraph_node, "m_sub");
  mid_graph->add_node(mid_out, "m_out");

  mid_graph->new_link("m_in", "value", "m_add", "a");
  mid_graph->new_link("m_const", "value", "m_add", "b");
  mid_graph->new_link("m_add", "sum", "m_sub", "in_val");
  mid_graph->new_link("m_sub", "out_val", "m_out", "value");

  auto mid_subgraph_node = std::make_shared<gnode::SubGraphNode>("MidSubGraph",
                                                                 mid_graph);
  mid_subgraph_node->expose_input(mid_in);
  mid_subgraph_node->expose_output(mid_out);

  // Top level: input 10 -> mid adds 5 (=15) -> inner doubles (=30)
  gnode::Graph top_graph("TopGraph");
  auto         top_in = std::make_shared<ValueNode>(10.0f);
  auto         top_add = std::make_shared<AddNode>();
  auto         top_one = std::make_shared<ValueNode>(1.0f);

  top_graph.add_node(top_in, "top_in");
  top_graph.add_node(mid_subgraph_node, "top_mid_sub");
  top_graph.add_node(top_one, "top_one");
  top_graph.add_node(top_add, "top_add");

  top_graph.new_link("top_in", "value", "top_mid_sub", "mid_in");
  top_graph.new_link("top_mid_sub", "mid_out", "top_add", "a");
  top_graph.new_link("top_one", "value", "top_add", "b");

  top_graph.update();

  float *final_val = top_add->get_value_ref<float>("sum");
  ASSERT_NE(final_val, nullptr);
  EXPECT_FLOAT_EQ(*final_val, 31.0f);
}

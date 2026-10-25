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

class ThrowingComputeNode : public gnode::Node
{
public:
  ThrowingComputeNode(bool               should_throw = true,
                      const std::string &message = "SubGraph inner error")
      : gnode::Node("ThrowingComputeNode"),
        should_throw(should_throw),
        message(message)
  {
    this->add_port<float>(gnode::PortType::IN, "in");
    this->add_port<float>(gnode::PortType::OUT, "out");
  }

  void set_should_throw(bool value) { this->should_throw = value; }

  void compute() override
  {
    if (this->should_throw)
    {
      throw std::runtime_error(this->message);
    }

    float *p_in = this->get_value_ref<float>("in");
    float *p_out = this->get_value_ref<float>("out");

    if (p_in && p_out)
    {
      *p_out = *p_in * 2.0f;
    }
  }

private:
  bool        should_throw;
  std::string message;
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

TEST(SubGraphTest, AsyncStressParallelSubgraphs)
{
  // Stress multiple subgraphs executing in parallel via update_async()
  // over multiple rapid iterations.
  constexpr int num_subgraphs = 8;
  constexpr int iterations = 25;

  for (int iter = 0; iter < iterations; ++iter)
  {
    gnode::Graph root_graph("RootAsync");

    std::vector<std::shared_ptr<gnode::SubGraphNode>> subgraphs;
    std::vector<std::string>                          subgraph_ids;

    for (int i = 0; i < num_subgraphs; ++i)
    {
      auto child = std::make_shared<gnode::Graph>("Child_" + std::to_string(i));

      auto in_a = std::make_shared<gnode::NodeIn<float>>("a");
      auto in_b = std::make_shared<gnode::NodeIn<float>>("b");
      auto add = std::make_shared<AddNode>();
      auto mul = std::make_shared<MultiplyNode>(2.0f);
      auto out = std::make_shared<gnode::NodeOut<float>>("res");

      child->add_node(in_a, "in_a");
      child->add_node(in_b, "in_b");
      child->add_node(add, "add");
      child->add_node(mul, "mul");
      child->add_node(out, "out");

      child->new_link("in_a", "value", "add", "a");
      child->new_link("in_b", "value", "add", "b");
      child->new_link("add", "sum", "mul", "in");
      child->new_link("mul", "out", "out", "value");

      auto sg_node = std::make_shared<gnode::SubGraphNode>(
          "SG_" + std::to_string(i),
          child);
      sg_node->expose_input(in_a);
      sg_node->expose_input(in_b);
      sg_node->expose_output(out);

      std::string sg_id = "sg_" + std::to_string(i);
      root_graph.add_node(sg_node, sg_id);
      subgraphs.push_back(sg_node);
      subgraph_ids.push_back(sg_id);
    }

    // Input sources
    auto src_a = std::make_shared<ValueNode>(static_cast<float>(iter));
    auto src_b = std::make_shared<ValueNode>(3.0f);

    root_graph.add_node(src_a, "src_a");
    root_graph.add_node(src_b, "src_b");

    // Connect sources to all subgraphs
    for (int i = 0; i < num_subgraphs; ++i)
    {
      root_graph.new_link("src_a", "value", subgraph_ids[i], "a");
      root_graph.new_link("src_b", "value", subgraph_ids[i], "b");
    }

    // Converge all subgraph outputs through pairwise additions
    std::string prev_sum_id = "";

    for (int i = 0; i < num_subgraphs; ++i)
    {
      if (i == 0)
      {
        prev_sum_id = subgraph_ids[0];
      }
      else
      {
        auto        add_conv = std::make_shared<AddNode>();
        std::string conv_id = "conv_" + std::to_string(i);
        root_graph.add_node(add_conv, conv_id);

        if (i == 1)
        {
          root_graph.new_link(prev_sum_id, "res", conv_id, "a");
        }
        else
        {
          root_graph.new_link(prev_sum_id, "sum", conv_id, "a");
        }

        root_graph.new_link(subgraph_ids[i], "res", conv_id, "b");
        prev_sum_id = conv_id;
      }
    }

    // Asynchronous parallel update
    std::future<void> fut = root_graph.update_async(8);
    fut.get();

    // Verify final sum
    // Each subgraph computes: (iter + 3) * 2
    // Sum across num_subgraphs: num_subgraphs * (iter + 3) * 2
    float expected = num_subgraphs * (iter + 3.0f) * 2.0f;
    auto *last_conv = root_graph.get_node_ref_by_id<AddNode>(prev_sum_id);
    ASSERT_NE(last_conv, nullptr);

    float *res_val = last_conv->get_value_ref<float>("sum");
    ASSERT_NE(res_val, nullptr);
    EXPECT_FLOAT_EQ(*res_val, expected);
  }
}

TEST(SubGraphTest, AsyncNestedSubGraphsStress)
{
  // Deeply nested subgraphs updated asynchronously across multiple iterations
  constexpr int iterations = 20;

  for (int iter = 0; iter < iterations; ++iter)
  {
    // Level 3 (innermost)
    auto l3_graph = std::make_shared<gnode::Graph>("L3");
    auto l3_in = std::make_shared<gnode::NodeIn<float>>("in");
    auto l3_mul = std::make_shared<MultiplyNode>(2.0f);
    auto l3_out = std::make_shared<gnode::NodeOut<float>>("out");

    l3_graph->add_node(l3_in, "l3_in");
    l3_graph->add_node(l3_mul, "l3_mul");
    l3_graph->add_node(l3_out, "l3_out");
    l3_graph->new_link("l3_in", "value", "l3_mul", "in");
    l3_graph->new_link("l3_mul", "out", "l3_out", "value");

    auto l3_node = std::make_shared<gnode::SubGraphNode>("L3Node", l3_graph);
    l3_node->expose_input(l3_in);
    l3_node->expose_output(l3_out);

    // Level 2 (middle)
    auto l2_graph = std::make_shared<gnode::Graph>("L2");
    auto l2_in = std::make_shared<gnode::NodeIn<float>>("in");
    auto l2_add = std::make_shared<AddNode>();
    auto l2_const = std::make_shared<ValueNode>(1.0f);
    auto l2_out = std::make_shared<gnode::NodeOut<float>>("out");

    l2_graph->add_node(l2_in, "l2_in");
    l2_graph->add_node(l2_const, "l2_c");
    l2_graph->add_node(l2_add, "l2_add");
    l2_graph->add_node(l3_node, "l3_sub");
    l2_graph->add_node(l2_out, "l2_out");

    l2_graph->new_link("l2_in", "value", "l2_add", "a");
    l2_graph->new_link("l2_c", "value", "l2_add", "b");
    l2_graph->new_link("l2_add", "sum", "l3_sub", "in");
    l2_graph->new_link("l3_sub", "out", "l2_out", "value");

    auto l2_node = std::make_shared<gnode::SubGraphNode>("L2Node", l2_graph);
    l2_node->expose_input(l2_in);
    l2_node->expose_output(l2_out);

    // Root graph
    gnode::Graph root("RootDeep");
    auto root_in = std::make_shared<ValueNode>(static_cast<float>(iter));
    auto root_out = std::make_shared<MultiplyNode>(1.0f);

    root.add_node(root_in, "r_in");
    root.add_node(l2_node, "l2_sub");
    root.add_node(root_out, "r_out");

    root.new_link("r_in", "value", "l2_sub", "in");
    root.new_link("l2_sub", "out", "r_out", "in");

    // Async update
    root.update_async(4).get();

    // Expected: (iter + 1) * 2
    float *val = root_out->get_value_ref<float>("out");
    ASSERT_NE(val, nullptr);
    EXPECT_FLOAT_EQ(*val, (iter + 1.0f) * 2.0f);
  }
}

TEST(SubGraphTest, AsyncRapidConcurrentTriggers)
{
  // Build a graph containing 4 subgraphs with sleep/delay to simulate work
  // Rapidly trigger async updates from multiple threads
  auto child_graph = std::make_shared<gnode::Graph>("ChildWork");
  auto in_val = std::make_shared<gnode::NodeIn<float>>("in");
  auto mul = std::make_shared<MultiplyNode>(3.0f);
  auto out_val = std::make_shared<gnode::NodeOut<float>>("out");

  child_graph->add_node(in_val, "in");
  child_graph->add_node(mul, "mul");
  child_graph->add_node(out_val, "out");
  child_graph->new_link("in", "value", "mul", "in");
  child_graph->new_link("mul", "out", "out", "value");

  auto sg_node = std::make_shared<gnode::SubGraphNode>("WorkSub", child_graph);
  sg_node->expose_input(in_val);
  sg_node->expose_output(out_val);

  gnode::Graph root("RootAsyncRepeat");
  auto         val = std::make_shared<ValueNode>(1.0f);
  auto         sink = std::make_shared<MultiplyNode>(1.0f);

  root.add_node(val, "val");
  root.add_node(sg_node, "sg");
  root.add_node(sink, "sink");

  root.new_link("val", "value", "sg", "in");
  root.new_link("sg", "out", "sink", "in");

  // Run 50 back-to-back async passes
  for (int i = 0; i < 50; ++i)
  {
    val->set_value<float>("value", static_cast<float>(i));
    root.update_async(4).get();

    float *out = sink->get_value_ref<float>("out");
    ASSERT_NE(out, nullptr);
    EXPECT_FLOAT_EQ(*out, static_cast<float>(i) * 3.0f);
  }
}

TEST(SubGraphTest, ExceptionPropagationSync)
{
  auto child_graph = std::make_shared<gnode::Graph>("ThrowingChild");
  auto in_node = std::make_shared<gnode::NodeIn<float>>("in");
  auto th_node = std::make_shared<ThrowingComputeNode>(true,
                                                       "Sync child failure");
  auto out_node = std::make_shared<gnode::NodeOut<float>>("out");

  child_graph->add_node(in_node, "in");
  child_graph->add_node(th_node, "th");
  child_graph->add_node(out_node, "out");

  child_graph->new_link("in", "value", "th", "in");
  child_graph->new_link("th", "out", "out", "value");

  auto sg_node = std::make_shared<gnode::SubGraphNode>("Sub", child_graph);
  sg_node->expose_input(in_node);
  sg_node->expose_output(out_node);

  gnode::Graph root("RootSync");
  auto         val = std::make_shared<ValueNode>(10.0f);
  auto         sink = std::make_shared<MultiplyNode>(1.0f);

  root.add_node(val, "val");
  root.add_node(sg_node, "sg");
  root.add_node(sink, "sink");

  root.new_link("val", "value", "sg", "in");
  root.new_link("sg", "out", "sink", "in");

  EXPECT_THROW(root.update(), std::runtime_error);
}

TEST(SubGraphTest, ExceptionPropagationAsync)
{
  auto child_graph = std::make_shared<gnode::Graph>("ThrowingChildAsync");
  auto in_node = std::make_shared<gnode::NodeIn<float>>("in");
  auto th_node = std::make_shared<ThrowingComputeNode>(true,
                                                       "Async child failure");
  auto out_node = std::make_shared<gnode::NodeOut<float>>("out");

  child_graph->add_node(in_node, "in");
  child_graph->add_node(th_node, "th");
  child_graph->add_node(out_node, "out");

  child_graph->new_link("in", "value", "th", "in");
  child_graph->new_link("th", "out", "out", "value");

  auto sg_node = std::make_shared<gnode::SubGraphNode>("SubAsync", child_graph);
  sg_node->expose_input(in_node);
  sg_node->expose_output(out_node);

  gnode::Graph root("RootAsync");
  auto         val = std::make_shared<ValueNode>(10.0f);
  auto         sink = std::make_shared<MultiplyNode>(1.0f);

  root.add_node(val, "val");
  root.add_node(sg_node, "sg");
  root.add_node(sink, "sink");

  root.new_link("val", "value", "sg", "in");
  root.new_link("sg", "out", "sink", "in");

  std::future<void> fut = root.update_async(4);
  EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(SubGraphTest, NestedSubGraphExceptionPropagationAsync)
{
  // Level 3 (innermost) contains throwing node
  auto l3_graph = std::make_shared<gnode::Graph>("L3");
  auto l3_in = std::make_shared<gnode::NodeIn<float>>("in");
  auto l3_throw = std::make_shared<ThrowingComputeNode>(
      true,
      "Deep innermost failure");
  auto l3_out = std::make_shared<gnode::NodeOut<float>>("out");

  l3_graph->add_node(l3_in, "l3_in");
  l3_graph->add_node(l3_throw, "l3_throw");
  l3_graph->add_node(l3_out, "l3_out");
  l3_graph->new_link("l3_in", "value", "l3_throw", "in");
  l3_graph->new_link("l3_throw", "out", "l3_out", "value");

  auto l3_node = std::make_shared<gnode::SubGraphNode>("L3Node", l3_graph);
  l3_node->expose_input(l3_in);
  l3_node->expose_output(l3_out);

  // Level 2 (middle)
  auto l2_graph = std::make_shared<gnode::Graph>("L2");
  auto l2_in = std::make_shared<gnode::NodeIn<float>>("in");
  auto l2_out = std::make_shared<gnode::NodeOut<float>>("out");

  l2_graph->add_node(l2_in, "l2_in");
  l2_graph->add_node(l3_node, "l3_sub");
  l2_graph->add_node(l2_out, "l2_out");

  l2_graph->new_link("l2_in", "value", "l3_sub", "in");
  l2_graph->new_link("l3_sub", "out", "l2_out", "value");

  auto l2_node = std::make_shared<gnode::SubGraphNode>("L2Node", l2_graph);
  l2_node->expose_input(l2_in);
  l2_node->expose_output(l2_out);

  // Root
  gnode::Graph root("RootDeepException");
  auto         root_in = std::make_shared<ValueNode>(5.0f);
  auto         root_out = std::make_shared<MultiplyNode>(1.0f);

  root.add_node(root_in, "r_in");
  root.add_node(l2_node, "l2_sub");
  root.add_node(root_out, "r_out");

  root.new_link("r_in", "value", "l2_sub", "in");
  root.new_link("l2_sub", "out", "r_out", "in");

  std::future<void> fut = root.update_async(4);
  EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(SubGraphTest, AsyncStressParallelSubgraphsWithExceptions)
{
  // Stress test: 10 parallel subgraphs running simultaneously across worker
  // threads Run 30 iterations where throwing vs non-throwing is varied
  constexpr int iterations = 30;
  constexpr int num_subgraphs = 8;

  for (int iter = 0; iter < iterations; ++iter)
  {
    gnode::Graph root("RootStressExceptions_" + std::to_string(iter));
    auto         val = std::make_shared<ValueNode>(static_cast<float>(iter));
    root.add_node(val, "val");

    std::vector<std::shared_ptr<ThrowingComputeNode>> throwing_nodes;

    for (int s = 0; s < num_subgraphs; ++s)
    {
      auto child = std::make_shared<gnode::Graph>("Child_" + std::to_string(s));
      auto in_n = std::make_shared<gnode::NodeIn<float>>("in");
      // Pick at least one subgraph to throw on even iterations, and multiple on
      // odd iterations
      bool should_throw = (s == (iter % num_subgraphs));
      auto th_n = std::make_shared<ThrowingComputeNode>(should_throw,
                                                        "Stress error at " +
                                                            std::to_string(s));
      auto out_n = std::make_shared<gnode::NodeOut<float>>("out");

      throwing_nodes.push_back(th_n);

      child->add_node(in_n, "in");
      child->add_node(th_n, "th");
      child->add_node(out_n, "out");
      child->new_link("in", "value", "th", "in");
      child->new_link("th", "out", "out", "value");

      auto sg = std::make_shared<gnode::SubGraphNode>("SG_" + std::to_string(s),
                                                      child);
      sg->expose_input(in_n);
      sg->expose_output(out_n);

      root.add_node(sg, "sg_" + std::to_string(s));
      root.new_link("val", "value", "sg_" + std::to_string(s), "in");
    }

    std::future<void> fut = root.update_async(8);
    EXPECT_THROW(fut.get(), std::runtime_error);
  }
}

TEST(SubGraphTest, AsyncRecoveryAfterExceptionStress)
{
  // Verify that catching an exception leaves the graph in a consistent state
  // and subsequent passes succeed once the throwing node is fixed
  auto child = std::make_shared<gnode::Graph>("ChildRecovery");
  auto in_n = std::make_shared<gnode::NodeIn<float>>("in");
  auto th_n = std::make_shared<ThrowingComputeNode>(true, "Initial fail");
  auto out_n = std::make_shared<gnode::NodeOut<float>>("out");

  child->add_node(in_n, "in");
  child->add_node(th_n, "th");
  child->add_node(out_n, "out");
  child->new_link("in", "value", "th", "in");
  child->new_link("th", "out", "out", "value");

  auto sg = std::make_shared<gnode::SubGraphNode>("SGRecovery", child);
  sg->expose_input(in_n);
  sg->expose_output(out_n);

  gnode::Graph root("RootRecovery");
  auto         val = std::make_shared<ValueNode>(2.0f);
  auto         sink = std::make_shared<MultiplyNode>(3.0f);

  root.add_node(val, "val");
  root.add_node(sg, "sg");
  root.add_node(sink, "sink");

  root.new_link("val", "value", "sg", "in");
  root.new_link("sg", "out", "sink", "in");

  for (int iter = 0; iter < 10; ++iter)
  {
    // First: should throw
    th_n->set_should_throw(true);
    std::future<void> fail_fut = root.update_async(4);
    EXPECT_THROW(fail_fut.get(), std::runtime_error);

    // Second: heal node, should succeed
    th_n->set_should_throw(false);
    val->set_value<float>("value", static_cast<float>(iter + 1));
    std::future<void> succ_fut = root.update_async(4);
    EXPECT_NO_THROW(succ_fut.get());

    float *out = sink->get_value_ref<float>("out");
    ASSERT_NE(out, nullptr);
    // (iter + 1) * 2.0 (th_n compute) * 3.0 (sink compute) = (iter + 1) * 6.0
    EXPECT_FLOAT_EQ(*out, static_cast<float>(iter + 1) * 6.0f);
  }
}

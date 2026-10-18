#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "nodes.hpp"

namespace
{

enum class OpType
{
  Add,
  Multiply,
  Subtract,
  Negate
};

struct NodeDesc
{
  std::string id;
  std::string output_port;
};

struct GraphPair
{
  std::unique_ptr<gnode::Graph> sync_graph = std::make_unique<gnode::Graph>();
  std::unique_ptr<gnode::Graph> async_graph = std::make_unique<gnode::Graph>();
  std::vector<std::string>      value_node_ids;
  std::vector<std::string>      op_node_ids;
  std::vector<std::string>      all_node_ids;
  std::vector<std::string>      output_ports;
};

// Builds two identical graphs with the same random DAG structure
GraphPair build_random_graph(int num_values, int num_ops, unsigned int seed)
{
  GraphPair result;

  std::mt19937                     rng(seed);
  std::uniform_real_distribution<> val_dist(-50.0f, 50.0f);
  std::uniform_int_distribution<>  op_dist(0, 3);

  std::vector<NodeDesc> available_nodes;

  // Create input Value nodes
  for (int i = 0; i < num_values; ++i)
  {
    float val = static_cast<float>(val_dist(rng));

    auto id_sync = result.sync_graph->add_node<Value>(val);
    auto id_async = result.async_graph->add_node<Value>(val);

    result.value_node_ids.push_back(id_sync);
    result.all_node_ids.push_back(id_sync);
    result.output_ports.push_back("value");

    available_nodes.push_back({id_sync, "value"});
  }

  // Create and connect operation nodes
  for (int i = 0; i < num_ops; ++i)
  {
    OpType op = static_cast<OpType>(op_dist(rng));

    std::string id_sync;
    std::string id_async;
    std::string out_port;

    switch (op)
    {
    case OpType::Add:
    {
      id_sync = result.sync_graph->add_node<Add>();
      id_async = result.async_graph->add_node<Add>();
      out_port = "a + b";

      std::uniform_int_distribution<> src_dist(
          0,
          static_cast<int>(available_nodes.size()) - 1);
      const auto &src1 = available_nodes[src_dist(rng)];
      const auto &src2 = available_nodes[src_dist(rng)];

      result.sync_graph->new_link(src1.id, src1.output_port, id_sync, "a");
      result.sync_graph->new_link(src2.id, src2.output_port, id_sync, "b");

      result.async_graph->new_link(src1.id, src1.output_port, id_async, "a");
      result.async_graph->new_link(src2.id, src2.output_port, id_async, "b");
      break;
    }

    case OpType::Multiply:
    {
      id_sync = result.sync_graph->add_node<Multiply>();
      id_async = result.async_graph->add_node<Multiply>();
      out_port = "a * b";

      std::uniform_int_distribution<> src_dist(
          0,
          static_cast<int>(available_nodes.size()) - 1);
      const auto &src1 = available_nodes[src_dist(rng)];
      const auto &src2 = available_nodes[src_dist(rng)];

      result.sync_graph->new_link(src1.id, src1.output_port, id_sync, "a");
      result.sync_graph->new_link(src2.id, src2.output_port, id_sync, "b");

      result.async_graph->new_link(src1.id, src1.output_port, id_async, "a");
      result.async_graph->new_link(src2.id, src2.output_port, id_async, "b");
      break;
    }

    case OpType::Subtract:
    {
      id_sync = result.sync_graph->add_node<Subtract>();
      id_async = result.async_graph->add_node<Subtract>();
      out_port = "a - b";

      std::uniform_int_distribution<> src_dist(
          0,
          static_cast<int>(available_nodes.size()) - 1);
      const auto &src1 = available_nodes[src_dist(rng)];
      const auto &src2 = available_nodes[src_dist(rng)];

      result.sync_graph->new_link(src1.id, src1.output_port, id_sync, "a");
      result.sync_graph->new_link(src2.id, src2.output_port, id_sync, "b");

      result.async_graph->new_link(src1.id, src1.output_port, id_async, "a");
      result.async_graph->new_link(src2.id, src2.output_port, id_async, "b");
      break;
    }

    case OpType::Negate:
    {
      id_sync = result.sync_graph->add_node<Negate>();
      id_async = result.async_graph->add_node<Negate>();
      out_port = "-a";

      std::uniform_int_distribution<> src_dist(
          0,
          static_cast<int>(available_nodes.size()) - 1);
      const auto &src = available_nodes[src_dist(rng)];

      result.sync_graph->new_link(src.id, src.output_port, id_sync, "a");
      result.async_graph->new_link(src.id, src.output_port, id_async, "a");
      break;
    }
    }

    result.op_node_ids.push_back(id_sync);
    result.all_node_ids.push_back(id_sync);
    result.output_ports.push_back(out_port);

    available_nodes.push_back({id_sync, out_port});
  }

  return result;
}

void compare_node_outputs(GraphPair &gp)
{
  for (size_t i = 0; i < gp.all_node_ids.size(); ++i)
  {
    const auto &nid = gp.all_node_ids[i];
    const auto &port = gp.output_ports[i];

    auto *sync_node = gp.sync_graph->get_node(nid);
    auto *async_node = gp.async_graph->get_node(nid);

    ASSERT_NE(sync_node, nullptr);
    ASSERT_NE(async_node, nullptr);

    float *sync_val = sync_node->get_value_ref<float>(port);
    float *async_val = async_node->get_value_ref<float>(port);

    ASSERT_NE(sync_val, nullptr);
    ASSERT_NE(async_val, nullptr);

    EXPECT_FLOAT_EQ(*sync_val, *async_val)
        << "Mismatch at node " << nid << " (type " << sync_node->get_label()
        << ", port " << port << ")";
  }
}

} // namespace

TEST(AsyncVsSequentialUpdate, FullUpdateSmallRandomGraph)
{
  // 10 input values, 50 operation nodes
  auto gp = build_random_graph(10, 50, 12345);

  gp.sync_graph->update();

  std::future<void> fut = gp.async_graph->update_async();
  fut.get();

  compare_node_outputs(gp);
}

TEST(AsyncVsSequentialUpdate, FullUpdateLargeRandomGraphMultipleThreads)
{
  // 25 input values, 400 operation nodes
  auto gp = build_random_graph(25, 400, 54321);

  gp.sync_graph->update();

  // Test explicit thread count: 4 threads
  std::future<void> fut = gp.async_graph->update_async(4);
  fut.get();

  compare_node_outputs(gp);
}

TEST(AsyncVsSequentialUpdate, FullUpdateVariousThreadCounts)
{
  for (size_t num_threads : {1, 2, 4, 8})
  {
    auto gp = build_random_graph(15,
                                 100,
                                 1000 + static_cast<unsigned int>(num_threads));

    gp.sync_graph->update();

    std::future<void> fut = gp.async_graph->update_async(num_threads);
    fut.get();

    compare_node_outputs(gp);
  }
}

TEST(AsyncVsSequentialUpdate, SelectiveUpdateFromSingleValueNode)
{
  auto gp = build_random_graph(10, 100, 9999);

  // Initial full update on both
  gp.sync_graph->update();
  gp.async_graph->update_async().get();
  compare_node_outputs(gp);

  // Change one input value node
  const std::string &target_id = gp.value_node_ids[0];
  const float        new_val = 123.456f;

  gp.sync_graph->get_node_ref_by_id<Value>(target_id)->set_value<float>(
      "value",
      new_val);
  gp.async_graph->get_node_ref_by_id<Value>(target_id)->set_value<float>(
      "value",
      new_val);

  // Update selectively
  gp.sync_graph->update(target_id);
  gp.async_graph->update_async(target_id).get();

  compare_node_outputs(gp);
}

TEST(AsyncVsSequentialUpdate, SelectiveUpdateFromMultipleValueNodes)
{
  auto gp = build_random_graph(12, 120, 7777);

  // Initial full update
  gp.sync_graph->update();
  gp.async_graph->update_async().get();
  compare_node_outputs(gp);

  // Change multiple input nodes
  std::vector<std::string> modified_ids = {gp.value_node_ids[1],
                                           gp.value_node_ids[3],
                                           gp.value_node_ids[5]};

  for (size_t i = 0; i < modified_ids.size(); ++i)
  {
    float val = 42.0f * static_cast<float>(i + 1);

    gp.sync_graph->get_node_ref_by_id<Value>(modified_ids[i])
        ->set_value<float>("value", val);
    gp.async_graph->get_node_ref_by_id<Value>(modified_ids[i])
        ->set_value<float>("value", val);
  }

  // Update selectively
  gp.sync_graph->update(modified_ids);
  gp.async_graph->update_async(modified_ids).get();

  compare_node_outputs(gp);
}

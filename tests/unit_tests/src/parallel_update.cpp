#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <set>
#include <thread>

#include "nodes.hpp"

// A node that sleeps to simulate work and records thread IDs
class SleepAdd : public gnode::Node
{
public:
  int             delay_ms = 0;
  std::thread::id last_thread_id;

  SleepAdd(int delay_ms = 10) : gnode::Node("SleepAdd"), delay_ms(delay_ms)
  {
    add_port<float>(gnode::PortType::IN, "a");
    add_port<float>(gnode::PortType::IN, "b");
    add_port<float>(gnode::PortType::OUT, "a + b");
  }

  void compute() override
  {
    last_thread_id = std::this_thread::get_id();

    if (delay_ms > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    auto *a = get_value_ref<float>("a");
    auto *b = get_value_ref<float>("b");
    auto *out = get_value_ref<float>("a + b");

    if (a && b) *out = *a + *b;
  }
};

TEST(ParallelGraphUpdate, DiamondConvergenceAndSpeedup)
{
  // Graph structure:
  //         v1 (10)       v2 (20)       v3 (30)
  //            \         /     \         /
  //             b1 (add)         b2 (add)
  //                 \           /
  //                 converge (add)
  gnode::Graph g;

  auto v1 = g.add_node<Value>(10.f);
  auto v2 = g.add_node<Value>(20.f);
  auto v3 = g.add_node<Value>(30.f);

  auto b1 = g.add_node<SleepAdd>(40);
  auto b2 = g.add_node<SleepAdd>(40);

  auto conv = g.add_node<SleepAdd>(0);

  g.new_link(v1, "value", b1, "a");
  g.new_link(v2, "value", b1, "b");

  g.new_link(v2, "value", b2, "a");
  g.new_link(v3, "value", b2, "b");

  g.new_link(b1, "a + b", conv, "a");
  g.new_link(b2, "a + b", conv, "b");

  std::future<void> fut = g.update_async(4);
  fut.get();

  // b1 output = 10 + 20 = 30
  // b2 output = 20 + 30 = 50
  // conv output = 30 + 50 = 80
  auto *res_node = g.get_node_ref_by_id<SleepAdd>(conv);
  ASSERT_NE(res_node, nullptr);
  float *result = res_node->get_value_ref<float>("a + b");
  ASSERT_NE(result, nullptr);
  EXPECT_FLOAT_EQ(*result, 80.f);

  auto *node_b1 = g.get_node_ref_by_id<SleepAdd>(b1);
  auto *node_b2 = g.get_node_ref_by_id<SleepAdd>(b2);
  ASSERT_NE(node_b1, nullptr);
  ASSERT_NE(node_b2, nullptr);
  EXPECT_NE(node_b1->last_thread_id, std::thread::id{});
  EXPECT_NE(node_b2->last_thread_id, std::thread::id{});
}

TEST(ParallelGraphUpdate, AsyncFutureBasic)
{
  gnode::Graph g;

  auto v1 = g.add_node<Value>(5.f);
  auto v2 = g.add_node<Value>(7.f);
  auto add = g.add_node<Add>();

  g.new_link(v1, "value", add, "a");
  g.new_link(v2, "value", add, "b");

  std::future<void> fut = g.update_async();
  fut.get();

  auto *add_node = g.get_node_ref_by_id<Add>(add);
  ASSERT_NE(add_node, nullptr);
  float *res = add_node->get_value_ref<float>("a + b");
  ASSERT_NE(res, nullptr);
  EXPECT_FLOAT_EQ(*res, 12.f);
}

TEST(ParallelGraphUpdate, SelectiveNodesUpdate)
{
  gnode::Graph g;

  auto v1 = g.add_node<Value>(1.f);
  auto v2 = g.add_node<Value>(2.f);
  auto add1 = g.add_node<Add>();
  auto add2 = g.add_node<Add>();

  g.new_link(v1, "value", add1, "a");
  g.new_link(v2, "value", add1, "b");
  g.new_link(add1, "a + b", add2, "a");
  g.new_link(v2, "value", add2, "b");

  g.update();

  // Now change v1 and update asynchronously only from v1 downstream
  g.get_node_ref_by_id<Value>(v1)->set_value<float>("value", 10.f);

  std::future<void> fut = g.update_async(v1);
  fut.get();

  auto *node = g.get_node_ref_by_id<Add>(add2);
  ASSERT_NE(node, nullptr);
  float *res = node->get_value_ref<float>("a + b");
  ASSERT_NE(res, nullptr);
  // (10 + 2) + 2 = 14
  EXPECT_FLOAT_EQ(*res, 14.f);
}

TEST(ParallelGraphUpdate, Cancellation)
{
  gnode::Graph g;

  auto v1 = g.add_node<Value>(1.f);
  auto v2 = g.add_node<Value>(2.f);
  auto add1 = g.add_node<Add>();
  auto add2 = g.add_node<Add>();

  g.new_link(v1, "value", add1, "a");
  g.new_link(v2, "value", add1, "b");
  g.new_link(add1, "a + b", add2, "a");
  g.new_link(v2, "value", add2, "b");

  // Cancel whenever add1 is about to run
  g.cancel_update_callback = [&add1](const std::string &nid)
  { return nid == add1; };

  std::future<void> fut = g.update_async();
  fut.get();

  auto *node_add1 = g.get_node_ref_by_id<Add>(add1);
  auto *node_add2 = g.get_node_ref_by_id<Add>(add2);

  // add1 and add2 should not have completed
  float *res1 = node_add1->get_value_ref<float>("a + b");
  float *res2 = node_add2->get_value_ref<float>("a + b");

  EXPECT_FLOAT_EQ(*res1, 0.f);
  EXPECT_FLOAT_EQ(*res2, 0.f);
}

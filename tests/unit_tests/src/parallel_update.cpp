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

TEST(ParallelGraphUpdate, CancellationWithConcurrentDownstreamScheduling)
{
  // Graph structure:
  // Branch 1: v1 -> a1 -> a2
  // Branch 2: v2 -> cancel_node
  // Run on 2 threads. While Branch 2 cancels, Branch 1 finishes a1 and
  // schedules a2 concurrently.
  for (int iter = 0; iter < 20; ++iter)
  {
    gnode::Graph g;

    auto v1 = g.add_node<Value>(1.f);
    auto v2 = g.add_node<Value>(2.f);
    auto a1 = g.add_node<SleepAdd>(2);
    auto a2 = g.add_node<Add>();
    auto cancel_node = g.add_node<SleepAdd>(0);

    g.new_link(v1, "value", a1, "a");
    g.new_link(v1, "value", a1, "b");
    g.new_link(a1, "a + b", a2, "a");
    g.new_link(v1, "value", a2, "b");

    g.new_link(v2, "value", cancel_node, "a");
    g.new_link(v2, "value", cancel_node, "b");

    // Cancel when cancel_node is about to run
    g.cancel_update_callback = [&cancel_node](const std::string &nid)
    { return nid == cancel_node; };

    std::future<void> fut = g.update_async(2);
    fut.get();
  }
}

class ThrowingNode : public gnode::Node
{
public:
  ThrowingNode() : gnode::Node("ThrowingNode")
  {
    this->add_port<float>(gnode::PortType::OUT, "out");
  }

  void compute() override
  {
    throw std::runtime_error("Node computational failure");
  }
};

TEST(ParallelGraphUpdate, ExceptionPropagation)
{
  gnode::Graph g;

  auto v1 = g.add_node<Value>(1.f);
  auto th = g.add_node<ThrowingNode>();
  auto add = g.add_node<Add>();

  g.new_link(v1, "value", add, "a");
  g.new_link(th, "out", add, "b");

  std::future<void> fut = g.update_async();
  EXPECT_THROW(fut.get(), std::runtime_error);
}

class DelayedThrowingNode : public gnode::Node
{
public:
  int delay_ms = 0;

  DelayedThrowingNode(int delay_ms = 10)
      : gnode::Node("DelayedThrowingNode"), delay_ms(delay_ms)
  {
    this->add_port<float>(gnode::PortType::OUT, "out");
  }

  void compute() override
  {
    if (this->delay_ms > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(this->delay_ms));
    }

    throw std::runtime_error("Delayed computational failure");
  }
};

TEST(ParallelGraphUpdate, CancellationFollowedByFailureInRunningNode)
{
  // Graph structure:
  // Branch 1: v1 -> DelayedThrowingNode (starts running, sleeps, then throws)
  // Branch 2: v2 -> cancel_node (immediately triggers cancellation via callback)
  // Ensure that even though cancellation happens first while DelayedThrowingNode
  // is already running, the exception from DelayedThrowingNode is reliably captured
  // and rethrown after pool workers are joined.
  for (int iter = 0; iter < 10; ++iter)
  {
    gnode::Graph g;

    auto v1 = g.add_node<Value>(1.f);
    auto th = g.add_node<DelayedThrowingNode>(10);
    auto cancel_node = g.add_node<SleepAdd>(0);

    g.new_link(v1, "value", cancel_node, "a");
    g.new_link(v1, "value", cancel_node, "b");

    g.cancel_update_callback = [&cancel_node](const std::string &nid)
    { return nid == cancel_node; };

    std::future<void> fut = g.update_async(2);
    EXPECT_THROW(fut.get(), std::runtime_error);
  }
}

class LifecycleNode : public gnode::Node
{
public:
  std::atomic<bool> pre_computed = false;
  std::atomic<bool> computed = false;

  LifecycleNode() : gnode::Node("LifecycleNode")
  {
    this->add_port<float>(gnode::PortType::OUT, "out");
  }

  void on_pre_async_compute() override
  {
    this->pre_computed = true;
  }

  void compute() override
  {
    this->computed = true;
    auto *out = get_value_ref<float>("out");
    if (out) *out = 42.f;
  }
};

TEST(ParallelGraphUpdate, GraphWorkerExecution)
{
  auto g = std::make_shared<gnode::Graph>("worker_test_graph");
  auto nid = g->add_node<LifecycleNode>();

  gnode::GraphWorker worker;
  std::atomic<bool> finished = false;
  g->update_finished = [&finished]() { finished = true; };

  bool requested = worker.request(*g);
  EXPECT_TRUE(requested);

  worker.finish();

  auto *node = g->get_node_ref_by_id<LifecycleNode>(nid);
  ASSERT_NE(node, nullptr);
  EXPECT_TRUE(node->pre_computed.load());
  EXPECT_TRUE(node->computed.load());
  EXPECT_TRUE(finished.load());
}


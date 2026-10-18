#include "gnode.hpp"
#include "nodes.hpp"
#include <gtest/gtest.h>

// Test case for basic Event/EventConnection functionality
TEST(EventSystem, BasicSubscriptionAndNotify)
{
  gnode::Event<int, std::string> event;
  int                            received_int = 0;
  std::string                    received_str = "";

  auto conn = event.subscribe(
      [&received_int, &received_str](int val, std::string str)
      {
        received_int = val;
        received_str = str;
      });

  event.notify(42, "hello");

  EXPECT_EQ(received_int, 42);
  EXPECT_EQ(received_str, "hello");
}

TEST(EventSystem, MultipleSubscribers)
{
  gnode::Event<double> event;
  int                  count = 0;
  double               total = 0.0;

  auto conn1 = event.subscribe(
      [&count, &total](double val)
      {
        count++;
        total += val;
      });

  auto conn2 = event.subscribe(
      [&count, &total](double val)
      {
        count++;
        total += val * 2;
      });

  event.notify(1.5);

  EXPECT_EQ(count, 2);
  EXPECT_DOUBLE_EQ(total, 4.5); // 1.5 + 3.0
}

TEST(EventSystem, AutomaticDisconnection)
{
  gnode::Event<int> event;
  int               received = 0;

  {
    auto conn = event.subscribe([&received](int val) { received = val; });
    event.notify(10);
    EXPECT_EQ(received, 10);
  } // conn is destroyed here, so it should auto-disconnect

  event.notify(20);
  EXPECT_EQ(received, 10); // should not receive 20
}

TEST(EventSystem, ManualDisconnection)
{
  gnode::Event<int> event;
  int               received = 0;

  auto conn = event.subscribe([&received](int val) { received = val; });
  event.notify(10);
  EXPECT_EQ(received, 10);

  conn.disconnect();

  event.notify(20);
  EXPECT_EQ(received, 10); // should not receive 20
}

TEST(EventSystem, OutlivedEventConnection)
{
  std::unique_ptr<gnode::Event<int>> event =
      std::make_unique<gnode::Event<int>>();
  int received = 0;

  gnode::EventConnection conn = event->subscribe([&received](int val)
                                                 { received = val; });
  event->notify(10);
  EXPECT_EQ(received, 10);

  // Destroy event
  event.reset();

  // Connection disconnect should be a safe no-op now
  EXPECT_NO_THROW(conn.disconnect());
}

TEST(EventSystem, NodeEvents)
{
  Add node;
  int before_calls = 0;
  int after_calls = 0;

  auto conn_before = node.pre_update_event.subscribe(
      [&before_calls](gnode::Node &n)
      {
        before_calls++;
        EXPECT_EQ(n.get_label(), "Add");
      });

  auto conn_after = node.post_update_event.subscribe(
      [&after_calls](gnode::Node &n)
      {
        after_calls++;
        EXPECT_EQ(n.get_label(), "Add");
      });

  // Not dirty by default, update won't run compute/events
  node.update();
  EXPECT_EQ(before_calls, 0);
  EXPECT_EQ(after_calls, 0);

  // Set dirty and update
  node.is_dirty = true;
  node.update();
  EXPECT_EQ(before_calls, 1);
  EXPECT_EQ(after_calls, 1);
}

TEST(EventSystem, GraphEvents)
{
  gnode::Graph graph("test_graph");
  auto         v1 = std::make_shared<Value>(1.0f);
  auto         v2 = std::make_shared<Value>(2.0f);
  auto         add = std::make_shared<Add>();

  std::string id_v1 = graph.add_node(v1);
  std::string id_v2 = graph.add_node(v2);
  std::string id_add = graph.add_node(add);

  graph.new_link(id_v1, "value", id_add, "a");
  graph.new_link(id_v2, "value", id_add, "b");

  std::vector<std::pair<std::string, bool>> update_sequence;

  auto conn = graph.graph_progress_event.subscribe(
      [&update_sequence](const std::string &node_id,
                         const std::vector<std::string> & /*sorted_ids*/,
                         bool before_update)
      { update_sequence.push_back({node_id, before_update}); });

  // Run update
  graph.update();

  // The topological order should be id_v1, id_v2, id_add, or id_v2, id_v1,
  // id_add Let's assert we got exactly 6 updates (before and after for each of
  // the 3 nodes)
  ASSERT_EQ(update_sequence.size(), 6);

  EXPECT_EQ(update_sequence[0].second, true);  // before v1/v2
  EXPECT_EQ(update_sequence[1].second, false); // after v1/v2
  EXPECT_EQ(update_sequence[2].second, true);  // before v1/v2
  EXPECT_EQ(update_sequence[3].second, false); // after v1/v2
  EXPECT_EQ(update_sequence[4].second, true);  // before add
  EXPECT_EQ(update_sequence[4].first, id_add);
  EXPECT_EQ(update_sequence[5].second, false); // after add
  EXPECT_EQ(update_sequence[5].first, id_add);
}

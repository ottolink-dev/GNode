/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

/**
 * @file event.hpp
 * @brief Thread-safe publisher-subscriber event stream.
 * @copyright Copyright (c) 2026
 */
#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace gnode
{

/**
 * @class EventConnection
 * @brief Manages the lifetime of a subscription to an Event.
 *
 * Automatically disconnects the subscription upon destruction unless
 * manually disconnected beforehand.
 */
class EventConnection
{
public:
  EventConnection() = default;

  /**
   * @brief Construct a connection with a custom disconnect callback.
   * @param disconnect Callable to execute when disconnecting.
   */
  EventConnection(std::function<void()> disconnect)
      : disconnect_(std::move(disconnect))
  {
  }

  /// Destructor automatically triggers the disconnect action.
  ~EventConnection() { disconnect(); }

  EventConnection(const EventConnection &) = delete;
  EventConnection &operator=(const EventConnection &) = delete;

  /// Move constructor.
  EventConnection(EventConnection &&other) noexcept
      : disconnect_(std::move(other.disconnect_))
  {
    other.disconnect_ = {};
  }

  /// Move assignment operator.
  EventConnection &operator=(EventConnection &&other) noexcept
  {
    if (this != &other)
    {
      disconnect();

      disconnect_ = std::move(other.disconnect_);
      other.disconnect_ = {};
    }

    return *this;
  }

  /**
   * @brief Manually disconnect the subscription.
   *
   * Triggers the stored callback and releases it. Safe to call multiple times.
   */
  void disconnect()
  {
    if (disconnect_)
    {
      disconnect_();
      disconnect_ = {};
    }
  }

private:
  std::function<void()> disconnect_;
};

/**
 * @class Event
 * @brief Thread-safe publisher-subscriber event stream.
 *
 * Allows multiple callbacks to subscribe and receive notifications of new
 * values/signals. It can be notified from another thread than the one that
 * subscribes: once a connection is disconnected, its callback is not called
 * anymore (the disconnection waits for a notification in progress).
 *
 * @tparam Args Argument types passed to subscribers when notified.
 */
template <typename... Args> class Event
{
public:
  using callback_t = std::function<void(Args...)>;
  using listener_id_t = std::size_t;

  Event() = default;

  Event(const Event &) = delete;
  Event &operator=(const Event &) = delete;

  Event(Event &&) noexcept = default;
  Event &operator=(Event &&) noexcept = default;

  /**
   * @brief Subscribe to the event.
   *
   * @param callback Function to be called when notify() is called.
   * @return EventConnection Managing the subscription lifetime.
   */
  EventConnection subscribe(callback_t callback)
  {
    std::lock_guard<std::recursive_mutex> lock(*mutex_);

    const listener_id_t id = next_id_++;

    listeners_.push_back({id, std::move(callback)});

    // Capture a weak handle to the Event's lifetime token. If the Event is
    // destroyed before the connection disconnects, the token is gone and the
    // disconnect becomes a safe no-op (prevents UAF on listeners_).
    std::weak_ptr<void> alive = alive_token_;

    return EventConnection(
        [this, id, alive]()
        {
          if (!alive.expired()) this->unsubscribe(id);
        });
  }

  /**
   * @brief Notify all subscribers with the given arguments.
   *
   * Copies the listener list internally to allow callbacks to safely
   * unsubscribe themselves during execution.
   */
  void notify(Args... args)
  {
    // held while the callbacks run, recursive so that they can subscribe or
    // disconnect themselves
    std::lock_guard<std::recursive_mutex> lock(*mutex_);

    // copy to allow callbacks to disconnect themselves safely
    auto listeners = listeners_;

    for (auto &listener : listeners)
    {
      listener.callback(args...);
    }
  }

private:
  struct Listener
  {
    listener_id_t id;
    callback_t    callback;
  };

  void unsubscribe(listener_id_t id)
  {
    std::lock_guard<std::recursive_mutex> lock(*mutex_);

    listeners_.erase(std::remove_if(listeners_.begin(),
                                    listeners_.end(),
                                    [id](const Listener &listener)
                                    { return listener.id == id; }),
                     listeners_.end());
  }

  std::vector<Listener> listeners_;

  listener_id_t next_id_ = 0;

  // guards the listeners (a pointer, so that the Event stays movable)
  std::unique_ptr<std::recursive_mutex> mutex_ =
      std::make_unique<std::recursive_mutex>();

  // Pure lifetime sentinel: destroyed together with the Event. A weak_ptr to
  // this token lets an outliving EventConnection detect a dead Event and skip
  // the unsubscribe (see subscribe()).
  std::shared_ptr<void> alive_token_ = std::make_shared<char>();
};

} // namespace gnode

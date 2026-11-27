// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>

namespace opensim::framework {
struct BackgroundTaskStatistics {
    std::uint64_t submitted = 0, completed = 0, failed = 0, discarded = 0;
    std::size_t queued = 0;
    bool active = false, closed = false, finished = false;
};

// One owned execution lane. Close discards waiting work and waits for an active
// task, except when called by that task itself. The worker retains only its
// separate state, so self-close and destruction never join the current thread.
class BackgroundTaskQueue {
  public:
    using Task = std::function<void()>;
    using Error = std::function<void(std::exception_ptr)>;
    explicit BackgroundTaskQueue(Error error = {});
    ~BackgroundTaskQueue();
    BackgroundTaskQueue(const BackgroundTaskQueue &) = delete;
    BackgroundTaskQueue &operator=(const BackgroundTaskQueue &) = delete;
    bool submit(Task task);
    void close();
    BackgroundTaskStatistics statistics() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace opensim::framework

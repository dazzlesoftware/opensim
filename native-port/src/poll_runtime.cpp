// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/poll_runtime.hpp"
#include <condition_variable>
#include <thread>
#include <algorithm>
namespace opensim::server {
namespace {
class RuntimeWatcher final : public PollWatcher {
  public:
    explicit RuntimeWatcher(std::function<void()> callback)
        : thread_([callback = std::move(callback)] { callback(); }) {
    }
    ~RuntimeWatcher() override {
        join();
    }
    void join() override {
        if (thread_.joinable()) {
            if (thread_.get_id() == std::this_thread::get_id())
                throw std::logic_error("Watcher cannot join itself");
            thread_.join();
        }
    }

  private:
    std::thread thread_;
};
class RuntimeEngine final : public PollJobEngine {
    struct State {
        PollManagerBackend::Worker worker;
        PollRuntimeBackend::Error error;
        std::int32_t hold, concurrency;
        std::mutex mutex;
        std::condition_variable available;
        std::deque<PollRequestPtr> jobs;
        std::size_t active = 0;
        bool running = true;
        bool cancelled = false;
        State(PollManagerBackend::Worker callback, PollRuntimeBackend::Error log, std::int32_t wait,
              std::int32_t workers)
            : worker(std::move(callback)), error(std::move(log)), hold(wait),
              concurrency(std::max(std::int32_t(1), workers)) {
            running = bool(worker);
        }
    };
    struct Thread {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

  public:
    RuntimeEngine(PollManagerBackend::Worker worker, std::int32_t hold, std::int32_t concurrency,
                  PollRuntimeBackend::Error error)
        : state_(std::make_shared<State>(std::move(worker), std::move(error), hold, concurrency)) {
    }
    ~RuntimeEngine() override {
        dispose();
        join();
    }
    void enqueue(PollRequestPtr request) override {
        auto state = state_;
        std::lock_guard lock(state->mutex);
        if (!state->running)
            return;
        state->jobs.push_back(std::move(request));
        // Reap completed idle workers instead of retaining handles indefinitely.
        for (auto i = threads_.begin(); i != threads_.end();) {
            if (i->done->load(std::memory_order_acquire)) {
                i->thread.join();
                i = threads_.erase(i);
            } else
                ++i;
        }
        if (state->active < static_cast<std::size_t>(state->concurrency) &&
            state->active < state->jobs.size()) {
            threads_.reserve(threads_.size() + 1);
            auto done = std::make_shared<std::atomic<bool>>(false);
            ++state->active;
            try {
                threads_.push_back({std::thread([state, done] {
                                        run(state);
                                        done->store(true, std::memory_order_release);
                                    }),
                                    done});
            } catch (...) {
                --state->active;
                throw;
            }
        }
        state->available.notify_one();
    }
    std::size_t count() const override {
        auto state = state_;
        std::lock_guard lock(state->mutex);
        return state->jobs.size();
    }
    void cancel() override {
        auto state = state_;
        std::lock_guard lock(state->mutex);
        if (!state->running || state->jobs.empty())
            return;
        state->jobs.clear();
        state->cancelled = true;
        state->available.notify_all();
    }
    void dispose() override {
        auto state = state_;
        {
            std::lock_guard lock(state->mutex);
            if (!state->running)
                return;
            state->running = false;
        }
        state->available.notify_all();
        int count = 100;
        while (--count > 0) {
            {
                std::lock_guard lock(state->mutex);
                if (state->active == 0)
                    break;
            }
            std::this_thread::yield();
        }
        std::lock_guard lock(state->mutex);
        state->jobs.clear();
        state->worker = {};
    }
    void join() override {
        for (auto& entry : threads_)
            if (entry.thread.joinable()) {
                // A worker can release its own final owner after source Dispose.
                if (entry.thread.get_id() == std::this_thread::get_id())
                    entry.thread.detach();
                else
                    entry.thread.join();
            }
    }

  private:
    static void run(const std::shared_ptr<State>& state) {
        for (;;) {
            PollRequestPtr request;
            PollManagerBackend::Worker worker;
            {
                std::unique_lock lock(state->mutex);
                if (!state->available.wait_for(lock, std::chrono::milliseconds(state->hold), [&] {
                        return !state->running || state->cancelled || !state->jobs.empty();
                    })) {
                    --state->active;
                    return;
                }
                if (!state->running || state->cancelled) {
                    --state->active;
                    return;
                }
                request = std::move(state->jobs.front());
                state->jobs.pop_front();
                worker = state->worker;
            }
            {
                std::lock_guard lock(state->mutex);
                if (!state->running) {
                    --state->active;
                    return;
                }
            }
            try {
                if (worker)
                    worker(std::move(request));
            } catch (const std::exception& e) {
                if (state->error)
                    try {
                        state->error(e);
                    } catch (...) {
                    }
            }
        }
    }
    std::shared_ptr<State> state_;
    std::vector<Thread> threads_;
};
}
PollRuntimeBackend::PollRuntimeBackend(Error error, std::function<void()> watchdog)
    : error_(std::move(error)), watchdog_(std::move(watchdog)) {
}
void PollRuntimeBackend::sleep(std::int32_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
void PollRuntimeBackend::watchdog_update() {
    if (watchdog_)
        watchdog_();
}
void PollRuntimeBackend::error(const std::exception& error) {
    if (error_)
        error_(error);
}
std::shared_ptr<PollJobEngine> PollRuntimeBackend::make_pool(Worker worker, const std::string&,
                                                             std::int32_t hold, std::int32_t concurrency) {
    return std::make_shared<RuntimeEngine>(std::move(worker), hold, concurrency, error_);
}
std::shared_ptr<PollWatcher> PollRuntimeBackend::start_watcher(std::function<void()> watcher) {
    return std::make_shared<RuntimeWatcher>(std::move(watcher));
}
}

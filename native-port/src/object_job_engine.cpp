// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/object_job_engine.hpp"
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
namespace opensim::framework {
struct ObjectJobEngine::State {
    std::mutex mutex;
    std::condition_variable available;
    std::deque<std::any> jobs;
    Callback callback;
    Error error;
    std::string name;
    int hold, concurrency;
    size_t active = 0;
    bool running = false, cancelled = false;
};
struct ObjectJobEngine::Thread {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
};
ObjectJobEngine::ObjectJobEngine(Callback callback, std::string name, int hold, int concurrency, Error error)
    : state_(std::make_shared<State>()) {
    state_->callback = std::move(callback);
    state_->error = std::move(error);
    state_->name = std::move(name);
    state_->hold = hold;
    state_->concurrency = std::max(1, concurrency);
    state_->running = bool(state_->callback);
}
ObjectJobEngine::~ObjectJobEngine() {
    dispose();
    for (auto& entry : threads_)
        if (entry->thread.joinable()) {
            if (entry->done->load(std::memory_order_acquire) &&
                entry->thread.get_id() != std::this_thread::get_id())
                entry->thread.join();
            else
                entry->thread.detach();
        }
}
bool ObjectJobEngine::enqueue(std::any job) {
    auto state = state_;
    std::lock_guard lock(state->mutex);
    if (!state->running)
        return false;
    state->jobs.push_back(std::move(job));
    for (auto it = threads_.begin(); it != threads_.end();)
        if ((*it)->done->load(std::memory_order_acquire)) {
            (*it)->thread.join();
            it = threads_.erase(it);
        } else
            ++it;
    if (state->active < size_t(state->concurrency) && state->active < state->jobs.size()) {
        auto entry = std::make_unique<Thread>();
        entry->done = std::make_shared<std::atomic<bool>>(false);
        auto done = entry->done;
        threads_.reserve(threads_.size() + 1);
        ++state->active;
        try {
            entry->thread = std::thread([state, done] {
                try {
                    run(state);
                } catch (...) {
                }
                done->store(true, std::memory_order_release);
            });
            threads_.push_back(std::move(entry));
        } catch (...) {
            --state->active;
            throw;
        }
    }
    state->available.notify_one();
    return true;
}
size_t ObjectJobEngine::count() const {
    std::lock_guard lock(state_->mutex);
    return state_->jobs.size();
}
size_t ObjectJobEngine::active_threads() const {
    std::lock_guard lock(state_->mutex);
    return state_->active;
}
void ObjectJobEngine::cancel() {
    std::lock_guard lock(state_->mutex);
    if (!state_->running || state_->jobs.empty())
        return;
    state_->jobs.clear();
    state_->cancelled = true;
    state_->available.notify_all();
}
void ObjectJobEngine::dispose() {
    auto state = state_;
    {
        std::lock_guard lock(state->mutex);
        if (!state->running)
            return;
        state->running = false;
    }
    state->available.notify_all();
    int spins = 100;
    while (--spins > 0) {
        {
            std::lock_guard lock(state->mutex);
            if (state->active == 0)
                break;
        }
        std::this_thread::yield();
    }
    std::lock_guard lock(state->mutex);
    state->jobs.clear();
    state->callback = {};
}
void ObjectJobEngine::run(std::shared_ptr<State> state) {
    for (;;) {
        std::any item;
        Callback callback;
        {
            std::unique_lock lock(state->mutex);
            if (!state->running || state->cancelled || state->hold < -1) {
                --state->active;
                return;
            }
            auto ready = [&] { return !state->running || state->cancelled || !state->jobs.empty(); };
            bool available = true;
            if (state->hold == -1)
                state->available.wait(lock, ready);
            else
                available = state->available.wait_for(lock, std::chrono::milliseconds(state->hold), ready);
            if (!available) {
                if (!state->jobs.empty())
                    continue;
                --state->active;
                return;
            }
            if (!state->running || state->cancelled) {
                --state->active;
                return;
            }
            item = std::move(state->jobs.front());
            state->jobs.pop_front();
            callback = state->callback;
            if (!callback) {
                --state->active;
                return;
            }
        }
        try {
            callback(std::move(item));
        } catch (const std::exception& e) {
            if (state->error)
                state->error(state->name, e);
        }
    }
}
}

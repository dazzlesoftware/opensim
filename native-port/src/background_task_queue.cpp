// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/background_task_queue.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace opensim::framework {
struct BackgroundTaskQueue::State {
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<Task> tasks;
    Error error;
    BackgroundTaskStatistics stats;
    std::thread::id worker;
    bool ready = false;
};

BackgroundTaskQueue::BackgroundTaskQueue(Error error) : state_(std::make_shared<State>()) {
    state_->error = std::move(error);
    std::thread worker([state = state_] {
        std::unique_lock lock(state->mutex);
        state->worker = std::this_thread::get_id();
        state->ready = true;
        state->changed.notify_all();
        for (;;) {
            state->changed.wait(lock, [&] { return state->stats.closed || !state->tasks.empty(); });
            if (state->stats.closed)
                break;
            auto task = std::move(state->tasks.front());
            state->tasks.pop_front();
            state->stats.active = true;
            lock.unlock();
            std::exception_ptr failure;
            try {
                task();
            } catch (...) {
                failure = std::current_exception();
            }
            // Release captures outside the queue lock: their destructors may
            // close this queue, including from the worker itself.
            task = {};
            if (failure && state->error)
                try {
                    state->error(failure);
                } catch (...) {
                }
            lock.lock();
            state->stats.active = false;
            ++state->stats.completed;
            if (failure)
                ++state->stats.failed;
            state->changed.notify_all();
        }
        state->stats.finished = true;
        state->changed.notify_all();
    });
    worker.detach();
    std::unique_lock lock(state_->mutex);
    state_->changed.wait(lock, [&] { return state_->ready; });
}

BackgroundTaskQueue::~BackgroundTaskQueue() {
    close();
}

bool BackgroundTaskQueue::submit(Task task) {
    if (!task)
        throw std::invalid_argument("Background task is empty");
    std::lock_guard lock(state_->mutex);
    if (state_->stats.closed)
        return false;
    state_->tasks.push_back(std::move(task));
    ++state_->stats.submitted;
    state_->changed.notify_one();
    return true;
}

void BackgroundTaskQueue::close() {
    const auto state = state_;
    std::deque<Task> discarded;
    std::unique_lock lock(state->mutex);
    if (!state->stats.closed) {
        state->stats.closed = true;
        state->stats.discarded += state->tasks.size();
        discarded.swap(state->tasks);
        state->changed.notify_all();
    }
    // Dropped captures can run arbitrary cleanup and must not own this lock.
    lock.unlock();
    discarded.clear();
    lock.lock();
    if (state->worker != std::this_thread::get_id())
        state->changed.wait(lock, [&] { return state->stats.finished; });
}

BackgroundTaskStatistics BackgroundTaskQueue::statistics() const {
    std::lock_guard lock(state_->mutex);
    auto result = state_->stats;
    result.queued = state_->tasks.size();
    return result;
}
} // namespace opensim::framework

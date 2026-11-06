/*
 * Copyright (c) Contributors, http://opensimulator.org/
 * See CONTRIBUTORS.TXT for a full list of copyright holders.
 *
 * - Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Neither the name of the openmetaverse.org nor the names
 *   of its contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opensim/framework/expiring_cache.hpp"
#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <unordered_map>
namespace opensim::framework {
namespace {
using Clock = std::chrono::steady_clock;
class NativeTimer final : public ExpiringCacheTimer {
    struct State {
        std::mutex mutex;
        std::condition_variable condition;
        bool stopped = false, armed = true;
        Clock::time_point deadline;
        std::function<void()> callback;
    };
    std::shared_ptr<State> state_;
    std::thread worker_;

  public:
    NativeTimer(std::function<void()> callback, std::int32_t due) : state_(std::make_shared<State>()) {
        state_->deadline = Clock::now() + std::chrono::milliseconds(due);
        state_->callback = std::move(callback);
        worker_ = std::thread([s = state_] {
            std::unique_lock lock(s->mutex);
            while (!s->stopped) {
                if (!s->armed) {
                    s->condition.wait(lock, [&] { return s->stopped || s->armed; });
                    continue;
                }
                auto deadline = s->deadline;
                if (s->condition.wait_until(
                        lock, deadline, [&] { return s->stopped || !s->armed || s->deadline != deadline; }))
                    continue;
                s->armed = false;
                auto callback = s->callback;
                lock.unlock();
                callback();
                lock.lock();
            }
        });
    }
    // System.Threading.Timer.Dispose cancels future ticks without waiting for an
    // already-running callback. The worker owns its separate state until it exits.
    ~NativeTimer() override {
        dispose();
        if (worker_.joinable())
            worker_.detach();
    }
    void change(std::int32_t due) override {
        std::lock_guard lock(state_->mutex);
        if (state_->stopped)
            throw std::runtime_error("Cannot access a disposed object.");
        state_->deadline = Clock::now() + std::chrono::milliseconds(due);
        state_->armed = true;
        state_->condition.notify_all();
    }
    void dispose() override {
        std::lock_guard lock(state_->mutex);
        state_->stopped = true;
        state_->armed = false;
        state_->condition.notify_all();
    }
};
[[noreturn]] void recursion(const char* text) {
    throw std::runtime_error(text);
}
}
ExpiringCacheBackend native_expiring_cache_backend() {
    return {[] { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); },
            [](auto callback, std::int32_t due) {
                return std::make_shared<NativeTimer>(std::move(callback), due);
            }};
}
struct ExpiringCacheLock::State {
    std::mutex mutex;
    std::condition_variable condition;
    bool disposed = false;
    std::thread::id writer, upgrade;
    std::unordered_map<std::thread::id, std::size_t> readers;
    std::size_t total_readers = 0, waiting_writers = 0, waiting = 0;
};
ExpiringCacheLock::ExpiringCacheLock() : state_(std::make_unique<State>()) {
}
ExpiringCacheLock::~ExpiringCacheLock() = default;
void ExpiringCacheLock::enter(Mode mode) {
    auto& s = *state_;
    std::unique_lock lock(s.mutex);
    auto self = std::this_thread::get_id();
    if (s.disposed)
        throw std::runtime_error("Cannot access a disposed object.");
    if (mode == Mode::Read) {
        if (s.writer == self)
            recursion("A read lock may not be acquired with the write lock held in this mode.");
        if (s.readers.contains(self))
            recursion("Recursive read lock acquisitions not allowed in this mode.");
        ++s.waiting;
        s.condition.wait(lock, [&] {
            return s.writer == std::thread::id{} && (s.waiting_writers == 0 || s.upgrade == self);
        });
        --s.waiting;
        s.readers[self] = 1;
        ++s.total_readers;
    } else if (mode == Mode::Upgradeable) {
        if (s.upgrade == self)
            recursion("Recursive upgradeable lock acquisitions not allowed in this mode.");
        if (s.writer == self)
            recursion(
                "Upgradeable lock may not be acquired with write lock held in this mode. Acquiring Upgradeable "
                "lock gives the ability to read along with an option to upgrade to a writer.");
        if (s.readers.contains(self))
            recursion("Upgradeable lock may not be acquired with read lock held.");
        ++s.waiting;
        s.condition.wait(lock, [&] {
            return s.writer == std::thread::id{} && s.upgrade == std::thread::id{} && s.waiting_writers == 0;
        });
        --s.waiting;
        s.upgrade = self;
    } else {
        if (s.writer == self)
            recursion("Recursive write lock acquisitions not allowed in this mode.");
        if (s.readers.contains(self))
            recursion(
                "Write lock may not be acquired with read lock held. This pattern is prone to deadlocks. Please "
                "ensure that read locks are released before taking a write lock. If an upgrade is necessary, use "
                "an upgrade lock in place of the read lock.");
        ++s.waiting_writers;
        ++s.waiting;
        s.condition.wait(lock, [&] {
            return s.writer == std::thread::id{} && s.total_readers == 0 &&
                   (s.upgrade == std::thread::id{} || s.upgrade == self);
        });
        --s.waiting_writers;
        --s.waiting;
        s.writer = self;
    }
}
void ExpiringCacheLock::exit(Mode mode) {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    auto self = std::this_thread::get_id();
    if (mode == Mode::Read) {
        if (!s.readers.erase(self))
            throw std::runtime_error("The read lock is being released without being held.");
        --s.total_readers;
    } else if (mode == Mode::Upgradeable) {
        if (s.upgrade != self)
            throw std::runtime_error("The upgradeable lock is being released without being held.");
        s.upgrade = {};
    } else {
        if (s.writer != self)
            throw std::runtime_error("The write lock is being released without being held.");
        s.writer = {};
    }
    s.condition.notify_all();
}
void ExpiringCacheLock::dispose() {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    auto self = std::this_thread::get_id();
    if (s.waiting || s.writer == self || s.upgrade == self || s.readers.contains(self))
        throw std::runtime_error(
            "The lock is being disposed while still being used. It either is being held by a thread and/or "
            "has active waiters waiting to acquire the lock.");
    s.disposed = true;
}
}

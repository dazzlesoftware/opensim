// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_expiring_entries.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace opensim::services {
namespace {
using Clock = std::function<double()>;
Clock clock_or_default(Clock clock) {
    if (clock)
        return clock;
    return [] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
}
std::int32_t narrow(double value) {
    return std::isfinite(value) && value >= -2147483648.0 && value < 2147483648.0
               ? static_cast<std::int32_t>(value)
               : INT32_MIN;
}
std::int32_t plus(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
template <class Value> struct EntriesState : std::enable_shared_from_this<EntriesState<Value>> {
    struct Entry {
        Value value;
        std::int32_t expiry;
    };
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::unordered_map<std::string, Entry> entries;
    Clock clock;
    const double epoch;
    const std::int32_t period;
    const bool background;
    bool disposed = false, timer_exists = false, running = false;
    std::optional<double> due;
    std::chrono::steady_clock::time_point physical_due;
    std::uint64_t generation = 0;
    EntriesState(std::int32_t check, Clock supplied, bool bg)
        : clock(clock_or_default(std::move(supplied))), epoch(clock()),
          period(std::max<std::int32_t>(500, check)), background(bg) {
    }
    void require_lock() const {
        if (disposed)
            throw FlotsamEntriesDisposed();
    }
    void stop_timer() {
        timer_exists = false;
        due.reset();
        ++generation;
        changed.notify_all();
    }
    void start_worker() {
        if (!background || running)
            return;
        running = true;
        try {
            std::thread([state = this->shared_from_this()] { state->run(); }).detach();
        } catch (...) {
            running = false;
            stop_timer();
            throw;
        }
    }
    void schedule(double now) {
        due = now + period;
        physical_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(period);
        ++generation;
        changed.notify_all();
    }
    void add(const std::string& key, Value value, std::int32_t ttl) {
        double now = 0;
        std::int32_t expiry = INT32_MIN;
        if (ttl > 0) {
            now = clock();
            expiry = plus(narrow(now - epoch), std::max(ttl, period));
        }
        std::lock_guard lock(mutex);
        require_lock();
        entries[key] = {std::move(value), expiry};
        if (!timer_exists) {
            if (ttl <= 0)
                now = clock();
            timer_exists = true;
            schedule(now);
            start_worker();
        }
    }
    bool get(const std::string& key, Value& value) const {
        std::lock_guard lock(mutex);
        require_lock();
        auto found = entries.find(key);
        if (found == entries.end()) {
            value = Value{};
            return false;
        }
        value = found->second.value;
        return true;
    }
    bool contains(const std::string& key) const {
        std::lock_guard lock(mutex);
        require_lock();
        return entries.contains(key);
    }
    bool remove(const std::string& key) {
        std::lock_guard lock(mutex);
        require_lock();
        bool removed = entries.erase(key) != 0;
        if (entries.empty())
            stop_timer();
        return removed;
    }
    void clear() {
        std::lock_guard lock(mutex);
        require_lock();
        entries.clear();
        stop_timer();
    }
    void dispose() {
        std::lock_guard lock(mutex);
        if (disposed)
            return;
        disposed = true;
        stop_timer();
    }
    std::int32_t count() const {
        std::lock_guard lock(mutex);
        return static_cast<std::int32_t>(entries.size());
    }
    std::optional<double> next_due() const {
        std::lock_guard lock(mutex);
        return due;
    }
    void purge(double now) {
        if (entries.empty()) {
            stop_timer();
            return;
        }
        auto relative = narrow(now - epoch);
        for (auto it = entries.begin(); it != entries.end();)
            if (it->second.expiry > 0 && it->second.expiry < relative)
                it = entries.erase(it);
            else
                ++it;
        if (entries.empty())
            stop_timer();
        else
            schedule(now);
    }
    void pump() {
        std::lock_guard lock(mutex);
        if (disposed || !due)
            return;
        auto now = clock();
        if (!(*due <= now))
            return;
        due.reset();
        purge(now);
    }
    void run() {
        std::unique_lock lock(mutex);
        for (;;) {
            if (disposed || !timer_exists || !due) {
                running = false;
                return;
            }
            auto version = generation;
            auto deadline = physical_due;
            if (changed.wait_until(lock, deadline, [&] {
                    return disposed || !timer_exists || !due || version != generation;
                }))
                continue;
            due.reset();
            purge(clock());
        }
    }
};
}
struct FlotsamKeyEntries::State final : EntriesState<bool> {
    using EntriesState::EntriesState;
};
struct FlotsamAssetEntries::State final : EntriesState<Asset> {
    using EntriesState::EntriesState;
};
FlotsamKeyEntries::FlotsamKeyEntries(std::int32_t check, Clock clock, bool background)
    : state_(std::make_shared<State>(check, std::move(clock), background)) {
}
FlotsamKeyEntries::~FlotsamKeyEntries() = default;
void FlotsamKeyEntries::add(const std::string& key, std::int32_t ttl) {
    state_->add(key, true, ttl);
}
bool FlotsamKeyEntries::contains(const std::string& key) const {
    return state_->contains(key);
}
bool FlotsamKeyEntries::remove(const std::string& key) {
    return state_->remove(key);
}
void FlotsamKeyEntries::clear() {
    state_->clear();
}
void FlotsamKeyEntries::pump() {
    state_->pump();
}
void FlotsamKeyEntries::dispose() {
    state_->dispose();
}
std::int32_t FlotsamKeyEntries::count() const {
    return state_->count();
}
double FlotsamKeyEntries::epoch() const {
    return state_->epoch;
}
std::int32_t FlotsamKeyEntries::check_period() const {
    return state_->period;
}
std::optional<double> FlotsamKeyEntries::next_due() const {
    return state_->next_due();
}
FlotsamAssetEntries::FlotsamAssetEntries(std::int32_t check, Clock clock, bool background)
    : state_(std::make_shared<State>(check, std::move(clock), background)) {
}
FlotsamAssetEntries::~FlotsamAssetEntries() = default;
void FlotsamAssetEntries::add(const std::string& key, Asset asset, std::int32_t ttl) {
    state_->add(key, std::move(asset), ttl);
}
bool FlotsamAssetEntries::try_get(const std::string& key, Asset& asset) const {
    return state_->get(key, asset);
}
bool FlotsamAssetEntries::remove(const std::string& key) {
    return state_->remove(key);
}
void FlotsamAssetEntries::clear() {
    state_->clear();
}
void FlotsamAssetEntries::pump() {
    state_->pump();
}
void FlotsamAssetEntries::dispose() {
    state_->dispose();
}
std::int32_t FlotsamAssetEntries::count() const {
    return state_->count();
}
double FlotsamAssetEntries::epoch() const {
    return state_->epoch;
}
std::int32_t FlotsamAssetEntries::check_period() const {
    return state_->period;
}
std::optional<double> FlotsamAssetEntries::next_due() const {
    return state_->next_due();
}
}

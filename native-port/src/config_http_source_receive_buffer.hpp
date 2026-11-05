// HttpConnection ArrayBuffer ownership during synchronous-copy disposal.
// Model overlapping fills without introducing a C++ data race.
#pragma once
#include <chrono>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <cstdint>
namespace opensim::framework {
class ConfigHttpSourceReceiveBuffer {
    using Clock = std::chrono::steady_clock;
    mutable std::mutex mutex_;
    std::vector<std::uint8_t> bytes_ = std::vector<std::uint8_t>(4096);
    std::optional<Clock::time_point> pending_;

public:
    void begin_drain() {
        std::lock_guard lock(mutex_);
        pending_ = Clock::now() + std::chrono::seconds(2);
    }
    bool expired(Clock::time_point now) const {
        std::lock_guard lock(mutex_);
        return pending_ && now >= *pending_;
    }
    std::optional<Clock::time_point> deadline() const {
        std::lock_guard lock(mutex_);
        return pending_;
    }
    std::optional<Clock::time_point> take_pending() {
        std::lock_guard lock(mutex_);
        return std::exchange(pending_, {});
    }
    void observe(std::span<const std::uint8_t> bytes) {
        std::lock_guard lock(mutex_);
        while (bytes_.size() < bytes.size())
            bytes_.resize(bytes_.size() * 2);
        std::copy(bytes.begin(), bytes.end(), bytes_.begin());
    }
    std::size_t capacity() const {
        std::lock_guard lock(mutex_);
        return bytes_.size();
    }
    std::vector<std::uint8_t> overlapping_fills(std::span<const std::uint8_t> drain,
                                                std::span<const std::uint8_t> synchronous) {
        std::lock_guard lock(mutex_);
        if (drain.size() + synchronous.size() > bytes_.size())
            throw std::out_of_range("Managed receive-buffer commit exceeds capacity");
        // Both reads captured offset zero before either Commit. The second fill
        // overwrites the prefix, but Commit advances by both counts.
        std::copy(drain.begin(), drain.end(), bytes_.begin());
        std::copy(synchronous.begin(), synchronous.end(), bytes_.begin());
        return {bytes_.begin(), bytes_.begin() + drain.size() + synchronous.size()};
    }
};
}

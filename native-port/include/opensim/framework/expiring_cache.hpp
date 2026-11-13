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

#pragma once
#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include <atomic>
#include <algorithm>
#include <type_traits>
#include <bit>
#include <cmath>
#include <functional>
#include <memory>
#include <limits>
#include <exception>
namespace opensim::framework {
class ExpiringCacheTimer {
  public:
    virtual ~ExpiringCacheTimer() = default;
    virtual void change(std::int32_t due) = 0;
    virtual void dispose() = 0;
};
struct ExpiringCacheBackend {
    std::function<double()> timestamp;
    std::function<std::shared_ptr<ExpiringCacheTimer>(std::function<void()>, std::int32_t)> timer;
};
ExpiringCacheBackend native_expiring_cache_backend();
class ExpiringCacheLock {
  public:
    enum class Mode { Read, Upgradeable, Write };
    ExpiringCacheLock();
    ~ExpiringCacheLock();
    void enter(Mode);
    void exit(Mode);
    void dispose();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
template <class K, class V, class Hash> struct ExpiringCacheProbe;
template <class K, class V, class Hash = std::hash<K>> class ExpiringCacheOS {
    using Mode = ExpiringCacheLock::Mode;
    struct State {
        ExpiringCacheBackend backend;
        opensim::framework::AtomicSharedPtr<ExpiringCacheLock> lock{std::make_shared<ExpiringCacheLock>()};
        opensim::framework::AtomicSharedPtr<ExpiringCacheTimer> timer;
        OrderedDictionary<K, std::int32_t, Hash> expires;
        OrderedDictionary<K, V, Hash> values;
        std::shared_ptr<std::vector<V>> array;
        std::atomic<std::int32_t> count = 0;
        double start;
        std::int32_t interval;
    };
    std::shared_ptr<State> state_;
    template <class T> static T& required(const std::shared_ptr<T>& value) {
        if (!value)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        return *value;
    }
    static std::int32_t integer(double value) {
        return !std::isfinite(value) || value < -2147483648.0 || value >= 2147483648.0
                   ? std::numeric_limits<std::int32_t>::min()
                   : static_cast<std::int32_t>(value);
    }
    static std::int32_t wrapping_add(std::int32_t a, std::int32_t b) {
        return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
    }
    static void key(const K& value) {
        if constexpr (requires { value.has_value(); }) {
            if (!value.has_value())
                throw std::invalid_argument("Value cannot be null. (Parameter 'key')");
        } else if constexpr (requires { value.get(); }) {
            if (!value)
                throw std::invalid_argument("Value cannot be null. (Parameter 'key')");
        }
    }
    template <class Fn>
    static auto locked(const std::shared_ptr<State>& s, Mode mode, Fn fn) -> decltype(fn()) {
        auto lock = s->lock.load();
        required(lock).enter(mode);
        std::exception_ptr failure;
        if constexpr (std::is_void_v<decltype(fn())>) {
            try {
                fn();
            } catch (...) {
                failure = std::current_exception();
            }
            required(s->lock.load()).exit(mode);
            if (failure)
                std::rethrow_exception(failure);
        } else {
            std::optional<decltype(fn())> result;
            try {
                result.emplace(fn());
            } catch (...) {
                failure = std::current_exception();
            }
            required(s->lock.load()).exit(mode);
            if (failure)
                std::rethrow_exception(failure);
            return std::move(*result);
        }
    }
    static void dispose_timer(const std::shared_ptr<State>& s) {
        auto timer = s->timer.load();
        if (timer) {
            timer->dispose();
            s->timer.store({});
        }
    }
    static void check_timer(const std::shared_ptr<State>& s) {
        if (!s->timer.load()) {
            std::weak_ptr<State> weak = s;
            auto timer = s->backend.timer(
                [weak] {
                    if (auto owner = weak.lock())
                        purge_state(owner);
                },
                s->interval);
            s->timer.store(std::move(timer));
        }
    }
    static std::int32_t expiration(const std::shared_ptr<State>& s, std::int32_t ms) {
        if (ms <= 0)
            return std::numeric_limits<std::int32_t>::min();
        return wrapping_add(integer(s->backend.timestamp() - s->start), std::max(ms, s->interval));
    }
    static void set_expire(const std::shared_ptr<State>& s, const K& k, std::int32_t expiry) {
        s->expires[k] = expiry;
        s->count.store(static_cast<std::int32_t>(s->expires.size()));
    }
    static void purge_state(const std::shared_ptr<State>& s) {
        locked(s, Mode::Upgradeable, [&] {
            if (s->expires.empty()) {
                dispose_timer(s);
                return;
            }
            auto now = integer(s->backend.timestamp() - s->start);
            std::vector<K> expired;
            for (const auto& entry : s->expires)
                if (entry.second > 0 && entry.second < now)
                    expired.push_back(entry.first);
            if (!expired.empty()) {
                locked(s, Mode::Write, [&] {
                    s->array.reset();
                    for (const auto& k : expired) {
                        s->expires.erase(k);
                        s->values.erase(k);
                    }
                    s->count.store(static_cast<std::int32_t>(s->expires.size()));
                });
                if (s->expires.empty())
                    dispose_timer(s);
                else
                    required(s->timer.load()).change(s->interval);
            } else
                required(s->timer.load()).change(s->interval);
        });
    }

  public:
    explicit ExpiringCacheOS(std::int32_t interval = 500,
                             ExpiringCacheBackend backend = native_expiring_cache_backend())
        : state_(std::make_shared<State>()) {
        state_->backend = std::move(backend);
        state_->start = state_->backend.timestamp();
        state_->interval = std::max<std::int32_t>(500, interval);
    }
    ExpiringCacheOS(const ExpiringCacheOS&) = delete;
    ExpiringCacheOS& operator=(const ExpiringCacheOS&) = delete;
    ~ExpiringCacheOS() {
        try {
            dispose();
        } catch (...) {
        }
    }
    void dispose() {
        auto s = state_;
        auto lock = s->lock.load();
        if (lock) {
            dispose_timer(s);
            lock->dispose();
            s->lock.store({});
        }
    }
    std::int32_t count() const {
        return state_->count.load();
    }
    void add(const K& k, const V& value) {
        auto s = state_;
        auto expiry = wrapping_add(integer(s->backend.timestamp() - s->start), s->interval);
        locked(s, Mode::Write, [&] {
            key(k);
            set_expire(s, k, expiry);
            s->values[k] = value;
            s->array.reset();
            check_timer(s);
        });
    }
    void add(const K& k, const V& value, std::int32_t ms) {
        auto s = state_;
        auto expiry = expiration(s, ms);
        locked(s, Mode::Write, [&] {
            key(k);
            set_expire(s, k, expiry);
            s->values[k] = value;
            s->array.reset();
            check_timer(s);
        });
    }
    void add_or_update(const K& k, const V& value) {
        add(k, value);
    }
    void add_or_update(const K& k, const V& value, std::int32_t seconds) {
        add(k, value, std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(seconds) * 1000u));
    }
    void add_or_update(const K& k, const V& value, double seconds) {
        add(k, value, integer(seconds * 1000.0));
    }
    bool remove(const K& k) {
        auto s = state_;
        return locked(s, Mode::Write, [&] {
            key(k);
            bool success = s->expires.erase(k);
            success = s->values.erase(k) || success;
            s->count.store(static_cast<std::int32_t>(s->expires.size()));
            if (success)
                s->array.reset();
            if (s->expires.empty())
                dispose_timer(s);
            return success;
        });
    }
    void clear() {
        auto s = state_;
        locked(s, Mode::Write, [&] {
            dispose_timer(s);
            s->expires.clear();
            s->values.clear();
            s->count.store(0);
            s->array.reset();
        });
    }
    bool contains_key(const K& k) {
        auto s = state_;
        return locked(s, Mode::Read, [&] {
            key(k);
            return s->expires.contains(k);
        });
    }
    bool contains(const K& k) {
        return contains_key(k);
    }
    bool contains_key(const K& k, std::int32_t ms) {
        auto s = state_;
        return locked(s, Mode::Upgradeable, [&] {
            key(k);
            if (!s->expires.contains(k))
                return false;
            return locked(s, Mode::Write, [&] {
                auto expiry = expiration(s, ms);
                set_expire(s, k, expiry);
                return true;
            });
        });
    }
    bool contains(const K& k, std::int32_t ms) {
        return contains_key(k, ms);
    }
    bool try_get_value(const K& k, V& value) {
        auto s = state_;
        return locked(s, Mode::Read, [&] {
            key(k);
            auto it = s->values.find(k);
            if (it == s->values.end()) {
                value = V{};
                return false;
            }
            value = it->second;
            return true;
        });
    }
    bool try_get_value(const K& k, std::int32_t ms, V& value) {
        auto s = state_;
        return locked(s, Mode::Upgradeable, [&] {
            key(k);
            auto it = s->values.find(k);
            if (it == s->values.end()) {
                value = V{};
                return false;
            }
            value = it->second;
            return locked(s, Mode::Write, [&] {
                auto expiry = expiration(s, ms);
                set_expire(s, k, expiry);
                return true;
            });
        });
    }
    V& try_get_or_default_value(const K& k, bool& existed) {
        auto s = state_;
        required(s->lock.load()).exit(Mode::Upgradeable);
        V* result = nullptr;
        std::exception_ptr failure;
        try {
            key(k);
            existed = s->values.contains(k);
            result = &s->values[k];
        } catch (...) {
            failure = std::current_exception();
        }
        required(s->lock.load()).exit(Mode::Upgradeable);
        if (failure)
            std::rethrow_exception(failure);
        return *result;
    }
    V& try_get_or_default_value(const K& k, std::int32_t ms, bool& existed) {
        auto s = state_;
        required(s->lock.load()).enter(Mode::Write);
        V* result = nullptr;
        std::exception_ptr failure;
        try {
            key(k);
            existed = s->values.contains(k);
            result = &s->values[k];
            auto expiry = expiration(s, ms);
            set_expire(s, k, expiry);
        } catch (...) {
            failure = std::current_exception();
        }
        required(s->lock.load()).enter(Mode::Write);
        if (failure)
            std::rethrow_exception(failure);
        return *result;
    }
    std::shared_ptr<std::vector<V>> values() {
        auto s = state_;
        return locked(s, Mode::Upgradeable, [&] {
            if (!s->array) {
                s->array = std::make_shared<std::vector<V>>();
                s->array->reserve(s->values.size());
                for (const auto& entry : s->values)
                    s->array->push_back(entry.second);
            }
            return s->array;
        });
    }

  private:
    friend struct ExpiringCacheProbe<K, V, Hash>;
};
}

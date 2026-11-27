// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/expiring_cache.hpp"

struct PendingCacheClock {
    struct Timer final : opensim::framework::ExpiringCacheTimer {
        std::function<void()> callback;
        bool disposed = false;

        explicit Timer(std::function<void()> tick) : callback(std::move(tick)) {}

        void change(std::int32_t) override {}

        void dispose() override {
            disposed = true;
        }
    };

    double now = 0;
    std::int32_t interval = 10000;
    std::shared_ptr<Timer> timer;

    std::shared_ptr<opensim::framework::ExpiringCacheBackend> backend() {
        return std::make_shared<opensim::framework::ExpiringCacheBackend>(opensim::framework::ExpiringCacheBackend{
            [this] { return now; },
            [this](auto callback, std::int32_t due) {
                if (due != interval)
                    throw std::runtime_error("Unexpected cache purge interval");
                timer = std::make_shared<Timer>(std::move(callback));
                return timer;
            }});
    }

    bool active() const {
        return timer && !timer->disposed;
    }

    void fire() {
        const auto current = timer;
        if (current && !current->disposed)
            current->callback();
    }
};

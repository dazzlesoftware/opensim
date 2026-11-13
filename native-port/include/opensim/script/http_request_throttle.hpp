// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <mutex>
#include <unordered_map>

namespace opensim::script {

struct HttpRequestThrottleSettings {
    float prim_rate = 1.f;
    float prim_burst = 3.f;
    float owner_rate = 25.f;
    float owner_burst = 5.f;
};

class HttpRequestThrottle {
public:
    explicit HttpRequestThrottle(HttpRequestThrottleSettings settings = {});
    bool allow(std::uint32_t prim, framework::Uuid owner, double now_seconds);
    void stop(std::uint32_t prim, double now_seconds);
    void clear();

private:
    struct Bucket {
        double last_time;
        float credit;

        bool consume(double now_seconds, float rate, float burst);
    };
    const HttpRequestThrottleSettings settings_;
    std::mutex mutex_;
    std::unordered_map<std::uint32_t, Bucket> prims_;
    std::unordered_map<framework::Uuid, Bucket, framework::UuidHash> owners_;
};

}

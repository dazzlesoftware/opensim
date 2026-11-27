// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/crossing_ban_cache.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <iostream>
#include <utility>

using namespace opensim;

namespace {
struct Timer : framework::ExpiringCacheTimer {
    std::function<void()> callback;
    std::vector<std::string> &trace;
    bool active = true;

    Timer(std::function<void()> f, int due, std::vector<std::string> &t) : callback(std::move(f)), trace(t) {
        trace.push_back("new:" + std::to_string(due));
    }

    void change(std::int32_t due) override {
        active = due >= 0;
        trace.push_back("change:" + std::to_string(due));
    }

    void dispose() override {
        active = false;
        trace.push_back("dispose");
    }

    void fire() {
        if (std::exchange(active, false))
            callback();
    }
};
} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto input = network::parse_osd_json(line);
        double now_ms = 0;
        std::vector<std::string> trace;
        std::vector<std::shared_ptr<Timer>> timers;
        scene::CrossingBanCache cache({[&] { return now_ms; },
                                       [&](auto callback, int due) {
                                           auto timer = std::make_shared<Timer>(std::move(callback), due, trace);
                                           timers.push_back(timer);
                                           return timer;
                                       }});
        network::Value results;
        results.kind = network::Value::Kind::Array;
        for (const auto &op : input.get("ops").array) {
            std::string result = "ok";
            try {
                const auto operation = op.get("op").text;
                if (operation == "time")
                    now_ms = network::osd_real(op.get("value")) * 1000.;
                else if (operation == "tick") {
                    const auto current = timers;
                    for (const auto &timer : current)
                        timer->fire();
                } else if (operation == "dispose")
                    cache.dispose();
                else {
                    const auto region = std::stoull(op.get("region").text);
                    const framework::Uuid agent(op.get("agent").text);
                    if (operation == "add")
                        cache.add(region, agent, network::osd_real(op.get("seconds")));
                    else if (operation == "remove")
                        cache.remove(region, agent);
                    else
                        result = cache.if_banned(region, agent) ? "1" : "0";
                }
            } catch (const std::exception &) {
                result = "throw";
            }
            results.array.emplace_back(result);
        }
        network::Value timing;
        timing.kind = network::Value::Kind::Array;
        for (const auto &entry : trace)
            timing.array.emplace_back(entry);
        std::cout << network::osd_json(network::Value::object({{"results", results}, {"timers", timing}}), true)
                  << '\n';
    }
}

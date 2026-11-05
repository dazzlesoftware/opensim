// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_destination.hpp"
#include <fstream>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace opensim;
using namespace services;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        server::IniConfig config;
        SqliteRegionStore regions(":memory:", true);
        GridService grid(regions, config, [] { return 1700000000; });
        GridRegion region;
        region.id = framework::Uuid::random();
        region.name = "Region";
        region.x = 25600;
        region.y = 25600;
        region.size_x = 512;
        region.size_y = 256;
        region.internal_endpoint = RegionEndpoint{"0.0.0.0", 9000};
        region.external_host = "127.0.0.1";
        check(grid.register_region({}, region).empty(), "register destination region");
        SqliteGridUserStore user_store(":memory:", true);
        GridUserService users(user_store, [] { return 1700000000; });
        const auto user = framework::Uuid::random().str();
        users.set_home(user, region.id, {-50, 9999, 5.5f}, {-1, 0, 1});
        users.set_last_position(user, {}, region.id, {-1, 9999, -3}, {1, -1, 0});
        auto info = users.info(user);
        LoginDestinationSelector selector(&grid);
        auto selected = selector.find({}, info, "home", grid.by_id({}, info->home_region));
        check(selected.region->id == region.id && selected.where == "home" &&
                  selected.position == framework::Vector3{-50, 9999, 5.5f} &&
                  selected.look_at == framework::Vector3{-1, 0, 1} && selected.flags == 160,
              "home location and flags are preserved without clamping");
        selected = selector.find({}, info, "last");
        check(selected.where == "last" && selected.position == framework::Vector3{.5f, 255.5f, -3} &&
                  selected.flags == 128,
              "last position clamps horizontal axes and preserves Z");
        users.set_home(user, framework::Uuid::random(), {1, 2, 3}, {});
        info = users.info(user);
        selected = selector.find({}, info, "home");
        check(selected.region->id == region.id && selected.where == "safe" &&
                  selected.position == framework::Vector3{128, 128, 0} && selected.flags == 4224,
              "online alternative selected when no defaults or home exist");
        LoginDestinationSelector restricted(&grid, false);
        check(!restricted.find({}, info, "home").region, "online fallback can be disabled");
        regions.set(region.id, "flags", "5");
        check(restricted.find({}, info, "home").region->id == region.id,
              "default region selection does not require online fallback permission");
        selected = selector.find({}, info, "uri:Region&1.5&2&3");
        check(selected.region->id == region.id && selected.where == "url" &&
                  selected.position == framework::Vector3{1.5f, 2, 3} && selected.flags == 4224,
              "named custom start through native grid queries");
        selected = selector.find({}, info, "uri:Missing&90&91&92");
        check(selected.where == "safe" && selected.position == framework::Vector3{90, 91, 92},
              "custom fallback retains requested coordinates");
        check(!selector.find({}, info, "uri:Region@remote.example&1&2&3").region,
              "direct HG custom starts remain disabled like source");
        selected = selector.find({}, info, "uri:Region&" + std::string(65536, '9') + "&2&3");
        check(selected.region && std::isinf(selected.position.x),
              "large valid coordinates do not require recursive matching");
        bool threw = false;
        try {
            selector.find({}, info, "");
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "malformed starts preserve source failure");
        regions.set(region.id, "sizeY", "0");
        threw = false;
        try {
            selector.find({}, info, "last");
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "invalid clamp bounds throw rather than invoking undefined behavior");
        check(!users.info(user)->online, "selection does not mark the user online before admission");
        LoginDestinationSelector unavailable(nullptr);
        selected = unavailable.find({}, info, std::nullopt);
        check(!selected.region && selected.where == "home" && selected.flags == 128,
              "missing grid returns initialized destination state");
        std::cout << "PASS: login destination and native persistence integration\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

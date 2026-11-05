// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_destination.hpp"
#include <bit>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace services;
framework::Uuid id(int n) {
    std::ostringstream out;
    out << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return framework::Uuid(out.str());
}
std::string bits(framework::Vector3 v) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << std::bit_cast<std::uint32_t>(v.x) << ','
        << std::setw(8) << std::bit_cast<std::uint32_t>(v.y) << ',' << std::setw(8)
        << std::bit_cast<std::uint32_t>(v.z);
    return out.str();
}
int main(int argc, char** argv) {
    try {
        const int seed = std::stoi(argv[1]);
        server::IniConfig config;
        NullRegionStore store;
        GridService grid(
            store, config, [] { return 1700000000; }, {},
            [](const std::string& uri) { return uri.find("local.example") != std::string::npos ? 1 : 0; },
            [](const std::string& host) -> std::optional<std::string> {
                return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
            });
        for (int i = 1; i <= 3; ++i) {
            GridRegion region;
            region.id = id(i);
            region.name = i == 1 ? "Alpha" : i == 2 ? "Default" : "Fallback";
            region.x = i == 1 ? 25600 : i == 2 ? 256000 : 255744;
            region.y = i == 1 ? 25600 : 256000;
            region.size_x = i == 1 ? (seed % 5 == 0 ? 0 : 512) : 256;
            region.size_y = i == 1 ? (seed % 7 == 0 ? 0 : 256) : 256;
            region.internal_endpoint = RegionEndpoint{"0.0.0.0", 9000};
            region.external_host = "127.0.0.1";
            auto row = std::make_shared<RegionData>(region_data(region));
            row->data["flags"] = std::to_string(i == 1   ? 4
                                                : i == 2 ? ((seed & 1) ? 5 : 0)
                                                         : ((seed & 2) ? 2 : 0) | ((seed & 4) ? 4 : 0) |
                                                               ((seed & 8) ? 512 : 0));
            store.store(row);
        }
        LoginDestinationSelector selector(&grid, (seed & 16) == 0);
        GridUserInfo info;
        info.home_region = info.last_region = id(1);
        info.home_position = {-50, 9999, 5.5f};
        info.home_look_at = {-1, 0, 1};
        info.last_look_at = {1, -1, 0};
        const auto nan = std::bit_cast<float>(0xffc00000u), infinity = std::bit_cast<float>(0x7f800000u);
        info.last_position = seed % 4 == 0   ? framework::Vector3{-100, 10000, -25}
                             : seed % 4 == 1 ? framework::Vector3{nan, nan, infinity}
                             : seed % 4 == 2 ? framework::Vector3{infinity, -infinity, 70}
                                             : framework::Vector3{128.25f, .25f, -0.f};
        const auto home = grid.by_id({}, id(1));
        const auto observe = [&](LoginDestinationSelector& target, const std::optional<std::string>& start,
                                 const std::optional<GridUserInfo>& user,
                                 const std::optional<GridRegion>& homeRegion) {
            try {
                const auto result = target.find({}, user, start, homeRegion);
                std::cout << (result.region ? result.region->id.str() : "-") << '|'
                          << (result.gatekeeper ? result.gatekeeper->id.str() : "-") << '|' << result.where
                          << '|' << bits(result.position) << '|' << bits(result.look_at) << '|'
                          << result.flags << '\n';
            } catch (...) {
                std::cout << "EX\n";
            }
        };
        if (argc > 2 && std::string(argv[2]) == "--stdin") {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::string value;
                for (std::size_t i = 0; i < line.size(); i += 2)
                    value.push_back(static_cast<char>(std::stoi(line.substr(i, 2), nullptr, 16)));
                observe(selector, value, info, std::nullopt);
            }
            return 0;
        }
        observe(selector, "home", info, home);
        observe(selector, "home", info, std::nullopt);
        observe(selector, "home", std::nullopt, home);
        observe(selector, "last", info, std::nullopt);
        info.last_region = {};
        observe(selector, "last", info, std::nullopt);
        info.last_region = id(99);
        observe(selector, "last", info, std::nullopt);
        observe(selector, "last", std::nullopt, std::nullopt);
        for (const auto& start : std::vector<std::optional<std::string>>{
                 "uri:Alpha&1.25&2.&300", "uri:Missing&90&91&92", "uri:Alpha@remote.example&1&2&3",
                 "uri:http://local.example/Alpha&1&2&3", "", "home ", "uri:Alpha&-1&2&3", "uri:Alpha&1&2&-3",
                 "uri:Alpha&1e2&2&3", "uri:Alpha&" + std::string(90, '9') + "&2&3",
                 "uri:Alpha&0." + std::string(400, '0') + "1&2&3", "uri:Alpha&1&2&3\n", "Alpha&1&2&3",
                 std::nullopt, "uri:&1&2&3", "uri:Alpha&\xD9\xA1&2&3",
                 "uri:http://remote.example/Alpha&1&2&3"})
            observe(selector, start, info, std::nullopt);
        LoginDestinationSelector unavailable(nullptr);
        observe(unavailable, std::nullopt, info, home);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

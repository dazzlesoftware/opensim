/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/grid_service.hpp"
#include "grid_fault_store.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
using namespace opensim;
using namespace services;
framework::Uuid id(int n) {
    std::ostringstream out;
    out << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return framework::Uuid(out.str());
}
void emit(const std::function<std::string()>& value) {
    std::string text;
    try {
        text = value();
    } catch (...) {
        text = "EX";
    }
    for (unsigned char c : text)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    std::cout << '\n';
}
std::string info(const std::optional<GridRegion>& region) {
    if (!region)
        return "-";
    std::string result;
    for (const auto& [key, value] : region->fields()) {
        if (!result.empty())
            result += ';';
        result += key + "=" + value.value_or("NULL");
    }
    return result + "|" + region->scope.str() + "|" + std::to_string(region->coord_x()) + "|" +
           std::to_string(region->coord_y()) + "|" + std::to_string(region->handle()) + "|" +
           std::to_string(region->maturity) + "|" + region->raw_server_uri.value_or("NULL");
}
std::string rows(const std::vector<GridRegion>& rows) {
    std::string result;
    for (const auto& row : rows) {
        if (!result.empty())
            result += ',';
        result += row.id.str();
    }
    return result;
}
std::string rawrows(const RegionRows& rows) {
    std::string result;
    for (const auto& row : rows) {
        if (!result.empty())
            result += ',';
        result += row->id.str();
    }
    return result;
}
GridRegion region(int n, std::string name, int x, int y, int sx = 256, int sy = 256) {
    GridRegion result;
    result.id = id(n);
    result.name = std::move(name);
    result.x = x;
    result.y = y;
    result.size_x = sx;
    result.size_y = sy;
    result.internal_endpoint = RegionEndpoint{"0.0.0.0", 9000};
    result.external_host = "127.0.0.1";
    result.http_port = 9001;
    result.token = "token";
    result.scope = id(8);
    return result;
}
std::string unhex(std::string_view input) {
    std::string result;
    for (std::size_t i = 0; i < input.size(); i += 2)
        result.push_back(static_cast<char>(std::stoi(std::string(input.substr(i, 2)), nullptr, 16)));
    return result;
}
int main(int argc, char** argv) {
    try {
        if (std::string(argv[1]) == "--model") {
            std::string line;
            while (std::getline(std::cin, line))
                emit([&] {
                    RegionFields fields;
                    std::size_t start = 0;
                    while (start < line.size()) {
                        const auto end = line.find('|', start);
                        const auto field = line.substr(start, end == std::string::npos ? end : end - start);
                        const auto split = field.find('=');
                        const auto value = field.substr(split + 1);
                        fields[unhex(field.substr(0, split))] =
                            value == "N" ? std::nullopt : std::optional{unhex(value)};
                        if (end == std::string::npos)
                            break;
                        start = end + 1;
                    }
                    return info(GridRegion::from_fields(fields));
                });
            return 0;
        }

        const int seed = std::stoi(argv[1]);
        const auto path = std::filesystem::temp_directory_path() /
                          ("opensim-grid-" + framework::Uuid::random().str() + ".ini");
        {
            std::ofstream file(path);
            file << "[GridService]\nRegionsAutoRegistration=" << ((seed & 8) == 0 ? "true" : "false")
                 << "\nAllowHypergridMapSearch=" << ((seed & 1) != 0 ? "true" : "false")
                 << "\nAllowDuplicateNames=" << ((seed & 1) != 0 ? "true" : "false")
                 << "\nDeleteOnUnregister=" << ((seed & 16) == 0 ? "true" : "false")
                 << "\nDefaultRegionFlags=+DefaultRegion +FallbackRegion\n";
            if ((seed & 1) == 0)
                file << "Region_Alpha=-FallbackRegion +DefaultHGRegion\n";
        }
        const auto config = server::IniConfig::load(path);
        std::filesystem::remove(path);
        FaultRegionStore store;
        GridService::Verify verify;
        if (seed & 4)
            verify = [&](const auto& principal, const auto& token, int time) {
                return (seed & 2) != 0 && principal == id(9) && token == "token" && time == 30;
            };
        GridService grid(
            store, config, [] { return 1700000000; }, verify,
            [](const std::string& uri) { return uri.find("local.example") != std::string::npos ? 1 : 0; },
            [](const std::string& host) -> std::optional<std::string> {
                return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
            },
            [](const auto&, auto& uri) -> std::optional<GridRegion> {
                if (uri.host != "remote.example")
                    return std::nullopt;
                GridRegion result;
                result.id = framework::Uuid("00000000-0000-0000-0000-000000000090");
                result.name = uri.region_name;
                result.internal_endpoint = RegionEndpoint{"0.0.0.0", 9000};
                result.external_host = "203.0.113.7";
                return result;
            });
        auto first = region(1, "Alpha", 25600, 25600, 256 * (1 + seed % 3), 256 * (1 + (seed / 3) % 2));
        auto second = region(2, "Alpha", 30000, 30000);
        auto other = region(3, "Beta", first.x + first.size_x, first.y);
        emit([&] { return grid.register_region({}, region(0, "Zero", 25600, 25600)); });
        emit([&] { return grid.register_region({}, region(4, "", 25600, 25600)); });
        emit([&] { return grid.register_region({}, region(4, "Reserved", 25600, 4096)); });
        if (seed & 8) {
            auto initial = std::make_shared<RegionData>(region_data(first));
            initial->data["flags"] = "0";
            store.store(initial);
        }
        emit([&] { return grid.register_region(id(8), first); });
        emit([&] { return info(grid.by_id(id(100), id(1))); });
        emit([&] { return grid.register_region(id(8), second); });
        emit([&] { return grid.register_region(id(8), other); });
        emit([&] { return grid.register_region(id(8), region(4, "Overlap", 25600, 25600)); });
        emit([&] { return info(grid.by_position(id(8), 25601, 25601)); });
        emit([&] { return info(grid.by_handle(id(8), first.handle())); });
        emit([&] { return rows(grid.neighbours(id(8), id(1))); });
        emit([&] { return rows(grid.range(id(8), 25599, 31001, 25599, 31001)); });
        emit([&] { return rows(grid.defaults(id(8))); });
        emit([&] { return rows(grid.hypergrid_defaults(id(8))); });
        emit([&] { return rows(grid.fallback(id(8), 25700, 25700)); });
        emit([&] { return rows(grid.online(id(8), 25700, 25700, 0)); });
        emit([&] { return rows(grid.online(id(8), 25700, 25700, 2)); });
        emit([&] { return rows(grid.hyperlinks(id(8))); });
        const int flags[] = {16, 64, 32, 128, 256, 512 | 4, 1024 | 1 | 4, 0};
        auto row = store.get(id(1), {});
        row->data["flags"] = std::to_string(flags[seed % 8]);
        row->data["PrincipalID"] = (seed % 4 == 0 ? framework::Uuid{} : id(9)).str();
        emit([&] { return grid.register_region(id(8), first); });
        emit([&] { return std::to_string(grid.flags(id(8), id(1))); });
        first.x += 8192;
        emit([&] { return grid.register_region(id(8), first); });
        emit([&] { return info(grid.by_id(id(8), id(1))); });
        emit([&] { return grid.deregister_region(id(1)) ? "1" : "0"; });
        emit([&] { return info(grid.by_id(id(8), id(1))); });
        emit([&] { return std::to_string(grid.flags(id(8), id(99))); });
        emit([&] {
            return std::to_string(parse_region_flags(
                3, "DefaultRegion -fallbackregion +PERSISTENT -DefaultRegion unknown +1024"));
        });
        emit([&] { return std::to_string(parse_region_flags(-1, "-2147483648 +0 -1")); });
        auto tall = region(10, "Tall", 1000, 10000, 256, 1024);
        auto tallrow = std::make_shared<RegionData>(region_data(tall));
        tallrow->data["flags"] = "5";
        store.store(tallrow);
        emit([&] { return rawrows(store.range(1000, 10500, 1001, 10501, id(999))); });
        emit([&] { return info(grid_region(*store.at(1000, 10500, id(999)))); });
        store.erase(id(2));
        auto replacement = std::make_shared<RegionData>(region_data(region(11, "Replacement", 50000, 50000)));
        replacement->data["flags"] = "5";
        store.store(replacement);
        emit([&] { return rawrows(*store.find("%", {})); });
        emit([&] { return store.find("missing", {}) ? "list" : "null"; });
        NullRegionStore isolated("!static");
        emit([&] { return rawrows(isolated.with_flags(DefaultRegion, {})); });
        emit([&] { return isolated.get(id(10), {}) ? "found" : "missing"; });
        emit([&] { return info(first.legacy_copy()); });
        emit([&] { return first.equals(first) ? "1" : "0"; });
        emit([&] { return GridRegion{}.equals(GridRegion{}) ? "1" : "0"; });
        auto failed = region(12, "Failed", 60000, 60000);
        store.store_false = true;
        emit([&] { return grid.register_region(id(8), failed); });
        emit([&] { return info(grid.by_id(id(8), id(12))); });
        store.store_false = false;
        store.store_throws = true;
        emit([&] { return grid.register_region(id(8), failed); });
        store.store_throws = false;
        auto moving = region(13, "Moving", 70000, 70000);
        auto movingrow = std::make_shared<RegionData>(region_data(moving));
        movingrow->data["flags"] = "4";
        store.store(movingrow);
        moving.x += 8192;
        store.erase_throws = true;
        emit([&] { return grid.register_region(id(8), moving); });
        store.erase_throws = false;
        store.erase_false = true;
        emit([&] { return grid.register_region(id(8), moving); });
        store.erase_false = false;
        emit([&] { return info(grid.by_id(id(8), id(13))); });
        store.set(id(13), "flags", "20");
        store.store_throws = true;
        emit([&] { return grid.deregister_region(id(13)) ? "1" : "0"; });
        store.store_throws = false;
        emit([&] { return info(grid.by_id(id(8), id(13))); });
        for (auto extra : {region(20, "Alpha Town", 80000, 80000), region(21, "Other Alpha", 82000, 80000),
                           region(22, "Alpha", 84000, 80000),
                           region(23, "http://remote.example:80/ World", 86000, 80000)}) {
            auto data = std::make_shared<RegionData>(region_data(extra));
            data->data["flags"] = "5";
            store.store(data);
        }
        emit([&] { return info(grid.by_name({}, "Alpha")); });
        emit([&] { return info(grid.by_name({}, ".Bad")); });
        emit([&] { return rows(grid.names({}, "Alpha", 2)); });
        emit([&] { return rows(grid.names({}, "Alpha", 0)); });
        emit([&] { return rows(grid.names({}, "Alpha", -1)); });
        emit([&] { return info(grid.by_name({}, "http://local.example/Alpha")); });
        emit([&] { return info(grid.local_by_name({}, "http://remote.example/World")); });
        emit([&] { return info(grid.by_name({}, "http://remote.example/World")); });
        emit([&] { return info(grid.by_name({}, "http://remote.example/Missing")); });
        emit([&] { return rows(grid.names({}, "http://remote.example/World", 3)); });
        emit([&] { return rows(grid.names({}, "http://remote.example/Missing", 3)); });
        emit([&] { return info(grid.by_name({}, "http://bad.invalid/Alpha")); });
        framework::RegionUri uri("http://local.example/Alpha");
        emit([&] {
            grid.by_uri({}, uri);
            return std::to_string(uri.flags) + "|" + uri.host + "|" + (uri.valid() ? "True" : "False");
        });
        for (int index = 0; index < 20; ++index) {
            auto row = std::make_shared<RegionData>(region_data(region(30 + index, "Tied", 256000, 256000)));
            row->data["flags"] = std::to_string(FallbackRegion | RegionOnline);
            store.store(row);
        }
        emit([&] { return rows(grid.fallback({}, 256000, 256000)); });
        emit([&] { return rows(grid.online({}, 256000, 256000, 10)); });
        store.store(std::make_shared<RegionData>(region_data(region(50, "\xc3\x89tage Annex", 90000, 90000))));
        store.store(std::make_shared<RegionData>(region_data(region(51, "\xc3\x89tage", 91000, 90000))));
        emit([&] { return info(grid.by_name({}, "\xc3\xa9tage")); });
        emit([&] { return rows(grid.names({}, "\xc3\xa9tage", 2)); });
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

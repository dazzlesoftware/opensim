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
#include "opensim/services/user_account.hpp"
#include "opensim/server/grid_identity.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <stdexcept>
namespace opensim::services {
std::function<std::optional<GridRegion>(std::string_view, bool)> make_script_grid_lookup(
    GridService& grid, framework::Uuid scope,
    const std::function<network::ServiceXmlValue(const OrderedRegionFields&)>& remote) {
    return [&grid, scope, &remote](std::string_view name, bool need_flags) -> std::optional<GridRegion> {
        if (!remote) {
            auto region = grid.by_name(scope, std::string(name));
            if (region && need_flags)
                region->flags = grid.flags(region->scope, region->id);
            return region;
        }
        std::optional<GridRegion> region;
        try {
            const auto reply = remote({{"SCOPEID", scope.str()}, {"NAME", std::string(name)},
                                       {"METHOD", "get_region_by_name"}});
            for (const auto& [key, value] : reply.fields) {
                if (key != "result" || !value.list)
                    continue;
                RegionFields fields;
                for (const auto& [field, entry] : value.fields)
                    fields[field] = entry.text;
                region = GridRegion::from_fields(fields);
                break;
            }
        } catch (const std::exception&) {
            return {};
        }
        if (region && need_flags) {
            region->flags = -1;
            try {
                const auto reply = remote({{"SCOPEID", region->scope.str()}, {"REGIONID", region->id.str()},
                                           {"METHOD", "get_region_flags"}});
                for (const auto& [key, value] : reply.fields) {
                    if (key != "result" || value.list)
                        continue;
                    std::int32_t flags;
                    if (parse_account_integer(value.text, flags))
                        region->flags = flags;
                    break;
                }
            } catch (const std::exception&) {
                // The original connector returns -1 for a failed flags request.
            }
        }
        return region;
    };
}

namespace {
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::string escape_like(const std::string& name) {
    std::string result;
    for (auto c : name) {
        if (c == '%' || c == '_')
            result += '\\';
        result += c;
    }
    return result;
}
std::vector<GridRegion> converted(const RegionRows& rows) {
    std::vector<GridRegion> result;
    for (const auto& row : rows)
        result.push_back(grid_region(*row));
    return result;
}
std::int32_t flags(const RegionData& row) {
    return parse_region_integer(row.data.at("flags"));
}
}
GridService::GridService(RegionStore& store, const server::IniConfig& config, Clock clock, Verify verify,
                         framework::RegionUri::LocalGrid local, framework::RegionUri::Resolver resolver,
                         Link linker)
    : store_(store), config_(config), clock_(clock ? clock : Clock{[] {
          return static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count());
      }}),
      verify_(std::move(verify)),
      auto_register_(config.boolean("GridService", "RegionsAutoRegistration", true)),
      delete_on_unregister_(auto_register_ && config.boolean("GridService", "DeleteOnUnregister", true)),
      allow_duplicate_names_(config.boolean("GridService", "AllowDuplicateNames", false)),
      local_(std::move(local)), resolver_(std::move(resolver)), linker_(std::move(linker)),
      allow_hypergrid_search_(config.boolean("GridService", "AllowHypergridMapSearch", false)) {
    if (!local_) {
        auto identity = std::make_shared<server::GridIdentity>(config, std::nullopt, resolver_);
        local_ = [identity](const std::string& value) { return identity->is_local_grid(value); };
    }
}
std::string GridService::register_region(const framework::Uuid& scope, const GridRegion& info) {
    std::lock_guard lock(mutex_);
    if (info.id.is_zero())
        return "Invalid RegionID - cannot be zero UUID";
    if (!info.name || info.name->empty())
        return "Ilegal region name";
    if (info.y <= 4096)
        return "Region location reserved for HG links coord Y must be higher than 16";
    const auto rows = store_.range(info.x, info.y, add(add(info.x, info.size_x), -1),
                                   add(add(info.y, info.size_y), -1), scope);
    std::shared_ptr<RegionData> existing;
    if (rows.size() > 1)
        return "Region overlaps another region";
    if (!rows.empty())
        existing = rows.front();
    if (existing && existing->id != info.id)
        return "Region overlaps another region";
    if (!auto_register_ && !existing)
        return "Region not found and automatic register disabled";
    if (existing) {
        auto value = services::flags(*existing);
        if (value & Reservation) {
            if (existing->data.at("PrincipalID") == framework::Uuid{}.str())
                return "Region location is reserved";
            value |= Authenticate;
        }
        if (value & Authenticate) {
            if (!verify_)
                return "No authentication possible";
            if (!verify_(framework::Uuid(existing->data.at("PrincipalID").value()), info.token, 30))
                return "Bad authentication";
        }
    }
    if (!allow_duplicate_names_) {
        const auto duplicate = store_.find(escape_like(*info.name), scope);
        if (duplicate)
            for (const auto& row : *duplicate)
                if (row->id != info.id)
                    return "Duplicate region name";
    }
    existing = store_.get(info.id, scope);
    if (existing && existing->id == info.id && (existing->x != info.x || existing->y != info.y)) {
        const auto value = services::flags(*existing);
        if (value & NoMove)
            return "Can't move this region";
        if (value & LockedOut)
            return "Region locked out";
        try {
            store_.erase(info.id);
        } catch (...) {
            return "[GRID SERVICE] Register failed to delete to update region " + info.id.str();
        }
    }
    auto row = std::make_shared<RegionData>(region_data(info));
    row->scope = scope;
    auto value = existing ? services::flags(*existing) & ~Reservation : 0;
    if (!config_.section("GridService").empty() && row->name && !row->name->empty()) {
        auto key = framework::trim_utf8(*row->name);
        std::replace(key.begin(), key.end(), ' ', '_');
        value = parse_region_flags(value, config_.get("GridService", "DefaultRegionFlags"));
        const auto by_name = config_.get("GridService", "Region_" + key);
        value = parse_region_flags(
            value, by_name.empty() ? config_.get("GridService", "Region_" + row->id.str()) : by_name);
    }
    value |= RegionOnline;
    row->data["flags"] = std::to_string(value);
    try {
        row->data["last_seen"] = std::to_string(clock_());
        store_.store(row);
    } catch (...) {
        return "[GRID SERVICE] Register failed to update region " + row->id.str();
    }
    return {};
}
bool GridService::deregister_region(const framework::Uuid& id) {
    std::lock_guard lock(mutex_);
    auto row = store_.get(id, {});
    if (!row)
        return false;
    auto value = services::flags(*row);
    if (!delete_on_unregister_ || (value & Persistent)) {
        value &= ~RegionOnline;
        row->data["flags"] = std::to_string(value);
        row->data["last_seen"] = std::to_string(clock_());
        try {
            store_.store(row);
        } catch (...) {
        }
        return true;
    }
    return store_.erase(id);
}
std::optional<GridRegion> GridService::by_id(const framework::Uuid& scope, const framework::Uuid& id) {
    std::lock_guard lock(mutex_);
    const auto row = store_.get(id, scope);
    return row ? std::optional{grid_region(*row)} : std::nullopt;
}
std::optional<GridRegion> GridService::by_position(const framework::Uuid& scope, std::int32_t x,
                                                   std::int32_t y) {
    std::lock_guard lock(mutex_);
    const auto snap = [](std::int32_t value) {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) & 0xffffff00u);
    };
    const auto row = store_.at(snap(x), snap(y), scope);
    return row ? std::optional{grid_region(*row)} : std::nullopt;
}
std::optional<GridRegion> GridService::by_handle(const framework::Uuid& scope, std::uint64_t handle) {
    std::lock_guard lock(mutex_);
    return by_position(scope, std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(handle >> 32)),
                       std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(handle)));
}
std::vector<GridRegion> GridService::neighbours(const framework::Uuid& scope, const framework::Uuid& id) {
    std::lock_guard lock(mutex_);
    const auto row = store_.get(id, scope);
    if (!row)
        return {};
    auto rows = store_.range(add(row->x, -1), add(row->y, -1), add(add(row->x, row->size_x), 1),
                             add(add(row->y, row->size_y), 1), scope);
    std::erase_if(rows, [&](const auto& value) {
        return value->id == id || (services::flags(*value) & Hyperlink) != 0;
    });
    return converted(rows);
}
std::vector<GridRegion> GridService::range(const framework::Uuid& scope, std::int32_t x1, std::int32_t x2,
                                           std::int32_t y1, std::int32_t y2) {
    std::lock_guard lock(mutex_);
    const auto snap = [](std::int32_t value) { return value / 256 * 256; };
    return converted(store_.range(snap(x1), snap(y1), snap(x2), snap(y2), scope));
}
std::vector<GridRegion> GridService::defaults(const framework::Uuid& scope) {
    std::lock_guard lock(mutex_);
    auto rows = store_.with_flags(DefaultRegion, scope);
    std::erase_if(rows, [](const auto& row) { return (services::flags(*row) & RegionOnline) == 0; });
    return converted(rows);
}
std::vector<GridRegion> GridService::hypergrid_defaults(const framework::Uuid& scope) {
    std::lock_guard lock(mutex_);
    auto rows = store_.with_flags(DefaultHGRegion, scope);
    std::erase_if(rows, [](const auto& row) { return (services::flags(*row) & RegionOnline) == 0; });
    auto result = converted(rows);
    auto normal = defaults(scope);
    result.insert(result.end(), normal.begin(), normal.end());
    return result;
}
std::vector<GridRegion> GridService::fallback(const framework::Uuid& scope, std::int32_t x, std::int32_t y) {
    std::lock_guard lock(mutex_);
    auto rows = store_.with_flags(FallbackRegion, scope);
    std::stable_sort(rows.begin(), rows.end(),
                     [&](const auto& a, const auto& b) { return compare_region_distance(*a, *b, x, y) < 0; });
    std::erase_if(rows, [](const auto& row) {
        const auto value = services::flags(*row);
        return (value & Hyperlink) != 0 || (value & RegionOnline) == 0;
    });
    return converted(rows);
}
std::vector<GridRegion> GridService::online(const framework::Uuid& scope, std::int32_t x, std::int32_t y,
                                            int maximum) {
    std::lock_guard lock(mutex_);
    auto rows = store_.with_flags(RegionOnline, scope);
    std::stable_sort(rows.begin(), rows.end(),
                     [&](const auto& a, const auto& b) { return compare_region_distance(*a, *b, x, y) < 0; });
    std::vector<GridRegion> result;
    for (const auto& row : rows)
        if ((services::flags(*row) & Hyperlink) == 0) {
            result.push_back(grid_region(*row));
            if (static_cast<int>(result.size()) >= maximum)
                break;
        }
    return result;
}
std::vector<GridRegion> GridService::hyperlinks(const framework::Uuid& scope) {
    std::lock_guard lock(mutex_);
    auto rows = store_.with_flags(Hyperlink, scope);
    std::erase_if(rows, [](const auto& row) { return (services::flags(*row) & RegionOnline) == 0; });
    return converted(rows);
}
std::int32_t GridService::flags(const framework::Uuid& scope, const framework::Uuid& id) {
    std::lock_guard lock(mutex_);
    const auto row = store_.get(id, scope);
    return row ? services::flags(*row) : -1;
}
std::optional<GridRegion> GridService::by_name(const framework::Uuid& scope,
                                               const std::optional<std::string>& name) {
    std::lock_guard lock(mutex_);
    framework::RegionUri uri(name);
    return by_uri(scope, uri);
}
std::optional<GridRegion> GridService::by_uri(const framework::Uuid& scope, framework::RegionUri& uri) {
    std::lock_guard lock(mutex_);
    if (!uri.valid())
        return std::nullopt;
    bool local = true;
    if (uri.has_host()) {
        if (!uri.resolve_dns(resolver_))
            return std::nullopt;
        local = local_ && local_(uri.host_url()) == 1;
        uri.set_local(local);
    }
    if (local) {
        if (uri.has_region_name()) {
            const auto row = store_.specific(uri.region_name, scope);
            if (row)
                return grid_region(*row);
        } else {
            auto regions = defaults(scope);
            return regions.at(0);
        }
        return std::nullopt;
    }
    if (!allow_hypergrid_search_)
        return std::nullopt;
    const auto map_name = uri.host_port_space_name();
    const auto found = store_.find("%" + escape_like(map_name), scope);
    const auto equal = [](std::string a, std::string b) {
        for (auto* value : {&a, &b})
            for (auto& c : *value)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
        return a == b;
    };
    if (found)
        for (const auto& row : *found) {
            const auto at = row->name.value().find("://");
            if (at != std::string::npos && equal(map_name, row->name->substr(at + 3)))
                return grid_region(*row);
        }
    return linker_ ? linker_(scope, uri) : std::nullopt;
}
std::optional<GridRegion> GridService::local_by_name(const framework::Uuid& scope,
                                                     const std::optional<std::string>& name) {
    std::lock_guard lock(mutex_);
    framework::RegionUri uri(name);
    return local_by_uri(scope, uri);
}
std::optional<GridRegion> GridService::local_by_uri(const framework::Uuid& scope, framework::RegionUri& uri) {
    std::lock_guard lock(mutex_);
    if (!uri.valid())
        return std::nullopt;
    if (uri.has_host()) {
        if (!uri.resolve_dns(resolver_) || !local_ || local_(uri.host_url()) != 1)
            return std::nullopt;
    }
    if (uri.has_region_name()) {
        const auto row = store_.specific(uri.region_name, scope);
        if (row)
            return grid_region(*row);
    } else {
        auto regions = defaults(scope);
        return regions.at(0);
    }
    return std::nullopt;
}
std::vector<GridRegion> GridService::names(const framework::Uuid& scope,
                                           const std::optional<std::string>& name, int maximum) {
    std::lock_guard lock(mutex_);
    framework::RegionUri uri(name);
    return names_uri(scope, uri, maximum);
}
std::vector<GridRegion> GridService::names_uri(const framework::Uuid& scope, framework::RegionUri& uri,
                                               int maximum) {
    std::lock_guard lock(mutex_);
    if (!uri.valid())
        return {};
    bool local = true;
    if (uri.has_host()) {
        if (!uri.resolve_dns(resolver_))
            return {};
        local = local_ && local_(uri.host_url()) == 1;
        uri.set_local(local);
        if (!uri.valid())
            return {};
    }
    int count = 0;
    const auto map_name = uri.host_port_space_name();
    const auto found = store_.find("%" + escape_like(map_name) + "%", scope);
    std::vector<GridRegion> result;
    const auto equal = [](std::string a, std::string b) {
        for (auto* value : {&a, &b})
            for (auto& c : *value)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
        return a == b;
    };
    const auto remove = [&] {
        if (count == maximum) {
            if (count <= 0 || static_cast<std::size_t>(count - 1) >= result.size())
                throw std::out_of_range("Region result index");
            result.erase(result.begin() + count - 1);
        }
    };
    if (local) {
        if (!uri.has_region_name()) {
            const auto regions = defaults(scope);
            if (!regions.empty())
                result.push_back(regions[0]);
        } else if (found)
            for (const auto& row : *found) {
                if (equal(uri.region_name, row->name.value())) {
                    result.insert(result.begin(), grid_region(*row));
                    remove();
                } else {
                    const int previous = count;
                    count = add(count, 1);
                    if (previous < maximum)
                        result.push_back(grid_region(*row));
                }
            }
        return result;
    }
    if (!allow_hypergrid_search_)
        return result;
    if (found && !found->empty()) {
        bool matched = false;
        for (const auto& row : *found) {
            const auto at = row->name.value().find("://");
            if (at == std::string::npos)
                continue;
            if (equal(map_name, row->name->substr(at + 3))) {
                matched = true;
                result.insert(result.begin(), grid_region(*row));
                remove();
            } else {
                const int previous = count;
                count = add(count, 1);
                if (previous < maximum)
                    result.push_back(grid_region(*row));
            }
        }
        if (matched)
            return result;
    }
    auto linked = linker_ ? linker_(scope, uri) : std::nullopt;
    if (linked) {
        remove();
        result.push_back(*linked);
    }
    return result;
}

}

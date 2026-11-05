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
#pragma once
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/region_store.hpp"
#include "opensim/framework/region_uri.hpp"
#include "opensim/server/config.hpp"
#include <functional>
#include <mutex>
namespace opensim::services {
class GridService {
public:
    using Clock = std::function<std::int32_t()>;
    using Verify = std::function<bool(const framework::Uuid&, const std::string&, int)>;
    using Link = std::function<std::optional<GridRegion>(const framework::Uuid&, framework::RegionUri&)>;
    GridService(RegionStore& store, const server::IniConfig& config, Clock clock = {}, Verify verify = {},
                framework::RegionUri::LocalGrid local = {}, framework::RegionUri::Resolver resolver = {},
                Link linker = {});
    std::optional<GridRegion> by_name(const framework::Uuid& scope, const std::optional<std::string>& name);
    std::optional<GridRegion> by_uri(const framework::Uuid& scope, framework::RegionUri& uri);
    std::optional<GridRegion> local_by_name(const framework::Uuid& scope,
                                            const std::optional<std::string>& name);
    std::optional<GridRegion> local_by_uri(const framework::Uuid& scope, framework::RegionUri& uri);
    std::vector<GridRegion> names(const framework::Uuid& scope, const std::optional<std::string>& name,
                                  int maximum);
    std::vector<GridRegion> names_uri(const framework::Uuid& scope, framework::RegionUri& uri, int maximum);
    std::string register_region(const framework::Uuid& scope, const GridRegion& region);
    bool deregister_region(const framework::Uuid& id);
    std::optional<GridRegion> by_id(const framework::Uuid& scope, const framework::Uuid& id);
    std::optional<GridRegion> by_position(const framework::Uuid& scope, std::int32_t x, std::int32_t y);
    std::optional<GridRegion> by_handle(const framework::Uuid& scope, std::uint64_t handle);
    std::vector<GridRegion> neighbours(const framework::Uuid& scope, const framework::Uuid& id);
    std::vector<GridRegion> range(const framework::Uuid& scope, std::int32_t x1, std::int32_t x2,
                                  std::int32_t y1, std::int32_t y2);
    std::vector<GridRegion> defaults(const framework::Uuid& scope);
    std::vector<GridRegion> hypergrid_defaults(const framework::Uuid& scope);
    std::vector<GridRegion> fallback(const framework::Uuid& scope, std::int32_t x, std::int32_t y);
    std::vector<GridRegion> online(const framework::Uuid& scope, std::int32_t x, std::int32_t y, int maximum);
    std::vector<GridRegion> hyperlinks(const framework::Uuid& scope);
    std::int32_t flags(const framework::Uuid& scope, const framework::Uuid& id);

private:
    std::recursive_mutex mutex_;
    RegionStore& store_;
    const server::IniConfig& config_;
    Clock clock_;
    Verify verify_;
    bool auto_register_, delete_on_unregister_, allow_duplicate_names_;
    framework::RegionUri::LocalGrid local_;
    framework::RegionUri::Resolver resolver_;
    Link linker_;
    bool allow_hypergrid_search_;
};
std::function<std::optional<GridRegion>(std::string_view, bool)> make_script_grid_lookup(
    GridService&, framework::Uuid,
    const std::function<network::ServiceXmlValue(const OrderedRegionFields&)>&);
}

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
#include <stdexcept>
using namespace opensim;
using namespace services;
void check(bool value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}
GridRegion region() {
    GridRegion value;
    value.id = framework::Uuid::random();
    value.name = "Region";
    value.x = 25600;
    value.y = 25600;
    value.internal_endpoint = RegionEndpoint{"0.0.0.0", 9000};
    value.external_host = "127.0.0.1";
    return value;
}
int main() {
    try {
        // !static is isolated for normal queries but the original flag helper
        // requires a process-wide instance, even before one has been constructed.
        NullRegionStore isolated("!static");
        bool threw = false;
        try {
            isolated.with_flags(DefaultRegion, {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "flags query before singleton must fail");
        auto model = GridRegion::from_fields({{"serverPort", "bad"}});
        check(model.size_x == 256 && model.size_y == 0 && model.internal_endpoint->port == 0,
              "dictionary constructor preserves source Y-default typo and port fallback");
        model.set_server_uri("");
        check(model.server_uri() == "/", "empty URI setter source slash behavior");
        model.raw_server_uri = std::nullopt;
        model.http_port = 9001;
        check(model.server_uri() == "http://127.0.0.1:9001/", "generated server URI");
        model.scope = framework::Uuid::random();
        model.token = "secret";
        auto copy = model.legacy_copy();
        check(copy.scope.is_zero() && copy.token.empty() && copy.raw_server_uri == model.server_uri(),
              "copy constructor omissions and generated URI normalization");
        model.x = -1;
        model.y = -257;
        check(model.coord_x() == 16777215 && model.coord_y() == 16777214 &&
                  model.handle() == 18446744073709551359ull,
              "unsigned region coordinates and handle");
        check(!GridRegion{}.equals(GridRegion{}), "zero IDs never equal");
        check(parse_region_flags(
                  3, "DefaultRegion -fallbackregion +PERSISTENT -DefaultRegion unknown +1024") == 1040,
              "flag enum parsing and signed operations");
        server::IniConfig config;
        FaultRegionStore store;
        GridService grid(store, config, [] { return 1700000000; });
        auto first = region();
        check(grid.register_region({}, GridRegion{}) == "Invalid RegionID - cannot be zero UUID",
              "invalid registration ID");
        check(grid.register_region({}, first).empty(), "registration success");
        check(grid.flags({}, first.id) == RegionOnline, "online flag applied");
        auto second = first;
        second.id = framework::Uuid::random();
        check(grid.register_region({}, second) == "Region overlaps another region", "overlap rejection");
        second.x += 8192;
        check(grid.register_region({}, second) == "Duplicate region name", "duplicate name rejection");
        check(grid.by_position({}, first.x + 255, first.y + 255)->id == first.id &&
                  !grid.by_position({}, first.x + 256, first.y),
              "position snapping and boundary behavior");
        check(grid.by_id(framework::Uuid::random(), first.id)->id == first.id, "Null provider ignores scope");
        store.set(first.id, "flags", "68");
        auto moved = first;
        moved.x += 8192;
        check(grid.register_region({}, moved) == "Can't move this region", "NoMove blocks relocation");
        store.set(first.id, "flags", "4");
        store.erase_throws = true;
        check(grid.register_region({}, moved).find("failed to delete") != std::string::npos,
              "relocation delete exception reported");
        store.erase_throws = false;
        store.erase_false = true;
        check(grid.register_region({}, moved).empty() && grid.by_id({}, first.id)->x == moved.x,
              "false delete result does not abort source registration");
        store.erase_false = false;
        store.set(first.id, "flags", "20");
        store.store_throws = true;
        check(grid.deregister_region(first.id) && !(*grid.by_id({}, first.id)->flags & RegionOnline),
              "persistent deregistration returns true despite failed write and keeps shared mutation");
        store.store_throws = false;
        auto failed = region();
        failed.x += 16384;
        failed.name = "Failed";
        store.store_false = true;
        check(grid.register_region({}, failed).empty() && !grid.by_id({}, failed.id),
              "source registration ignores false store result");
        store.store_false = false;
        store.store_throws = true;
        check(grid.register_region({}, failed).find("failed to update") != std::string::npos,
              "store exception response");
        store.store_throws = false;
        check(!isolated.get(first.id, {}), "isolated provider does not share normal dictionary");
        store.set(first.id, "flags", "5");
        check(!isolated.with_flags(DefaultRegion, {}).empty(),
              "isolated provider flag helper delegates to singleton");
        std::cout << "PASS: grid model, registry and provider failure paths\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

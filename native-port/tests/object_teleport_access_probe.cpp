#include "opensim/scene/object_teleport_access.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
using namespace opensim;

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto f = network::parse_osd_json(line);
        std::optional<scene::ObjectTeleportSourceLand> land;
        if (f.get("land").boolean && f.get("data").boolean)
            land =
                scene::ObjectTeleportSourceLand{framework::Uuid(f.get("landOwner").text),
                                                framework::Uuid(f.get("landGroup").text), f.get("groupOwned").boolean};
        int calls = 0;
        std::function<bool(framework::Uuid)> estate;
        if (f.get("estate").boolean)
            estate = [&](auto owner) {
                if (owner != framework::Uuid(f.get("owner").text))
                    throw std::runtime_error("Wrong estate query owner");
                ++calls;
                return f.get("manager").boolean;
            };
        const bool allowed = scene::object_teleport_source_land_access(
            framework::Uuid(f.get("owner").text), framework::Uuid(f.get("group").text), land, estate);
        std::cout << network::osd_json(network::Value::object({{"allowed", network::Value(allowed)},
                                                               {"estateQueries", network::Value(calls)}}),
                                       true)
                  << '\n';
    }
}

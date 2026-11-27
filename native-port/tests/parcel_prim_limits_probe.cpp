#include "opensim/scene/parcel_prim_limits.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
using namespace opensim;

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto f = network::parse_osd_json(line);
        const auto id = [&](const char *name) { return framework::Uuid(f.get(name).text); };
        const auto integer = [&](const char *name) { return f.get(name).integer; };
        std::vector<framework::Uuid> managers;
        if (f.get("manager").boolean)
            managers.push_back(id("owner"));
        const auto result = scene::parcel_prim_limit_check(
            integer("added"), id("owner"), integer("simulator"), integer("capacity"), integer("user"),
            integer("maximum"), id("parcelOwner"), f.get("groupOwned").boolean, id("estateOwner"), managers);
        network::Value denial;
        if (result == scene::ParcelPrimLimitDenial::ParcelFull)
            denial = network::Value("Unable to rez object because the parcel is full");
        if (result == scene::ParcelPrimLimitDenial::UserLimit)
            denial = network::Value("Unable to rez object because you have reached your limit");
        std::cout << network::osd_json(network::Value::object({{"denial", denial}}), true) << '\n';
    }
}

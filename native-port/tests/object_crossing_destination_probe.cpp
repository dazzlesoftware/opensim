#include "opensim/scene/object_crossing_destination.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>
using namespace opensim;

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto f = network::parse_osd_json(line);
        const auto integer = [&](const char *key) { return f.get(key).integer; };
        const auto real = [&](const char *key) {
            return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(f.get(key).text)));
        };
        framework::Vector3 arrival;
        network::Value queries;
        queries.kind = network::Value::Kind::Array;
        bool found = false;
        std::string outcome = "return";
        try {
            found =
                scene::object_crossing_destination(
                    f.get("scene").boolean, integer("sourceX"), integer("sourceY"), {real("x"), real("y"), real("z")},
                    [&](auto x, auto y) -> std::optional<services::GridRegion> {
                        network::Value q;
                        q.kind = network::Value::Kind::Array;
                        q.array = {network::Value(x), network::Value(y)};
                        queries.array.push_back(std::move(q));
                        if (f.get("throws").boolean)
                            throw std::runtime_error("lookup");
                        if (!f.get("found").boolean)
                            return std::nullopt;
                        services::GridRegion region;
                        region.x = integer("destX");
                        region.y = integer("destY");
                        region.size_x = integer("sizeX");
                        region.size_y = integer("sizeY");
                        return region;
                    },
                    arrival)
                    .has_value();
        } catch (const std::exception &) {
            outcome = "throw";
        }
        network::Value bits;
        bits.kind = network::Value::Kind::Array;
        for (float value : {arrival.x, arrival.y, arrival.z})
            bits.array.emplace_back(std::to_string(std::bit_cast<std::uint32_t>(value)));
        std::cout << network::osd_json(network::Value::object({{"outcome", outcome},
                                                               {"found", network::Value(found)},
                                                               {"queries", queries},
                                                               {"position", bits}}),
                                       true)
                  << '\n';
    }
}

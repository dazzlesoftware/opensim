#include "opensim/scene/object_teleport_motion.hpp"
#include "opensim/scene/world.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <iostream>
using namespace opensim;

int main(int argc, char **argv) {
    const bool keyframed_world = argc > 1 && std::string_view(argv[1]) == "--keyframed-world";
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto frame = network::parse_osd_json(line);
        const auto real = [&](const network::Value &value) {
            return frame.get("bits").boolean ? std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(value.text)))
                                             : float(network::osd_real(value));
        };
        const auto vector = [&](const char *name) {
            const auto &a = frame.get(name).array;
            return framework::Vector3{real(a.at(0)), real(a.at(1)), real(a.at(2))};
        };
        const auto quaternion = [&](const char *name) {
            const auto &a = frame.get(name).array;
            return framework::Quaternion{real(a.at(0)), real(a.at(1)), real(a.at(2)), real(a.at(3))};
        };
        const scene::ObjectTeleportMotion current{vector("velocity"), vector("angular"), vector("acceleration"),
                                                  quaternion("current")};
        auto result = frame.get("storage").boolean
                          ? scene::object_teleport_root_storage(current)
                          : scene::prepare_object_teleport_motion(current, quaternion("rotation"),
                                                                  network::osd_integer(frame.get("flags")));
        if (keyframed_world) {
            scene::World world;
            auto root = scene::SceneObject::primitive(framework::Uuid::random(), {10, 20, 30});
            root.acceleration = current.acceleration;
            root = world.add(root).value();
            if (!world.store_keyframed_properties(root, current.orientation, current.velocity,
                                                  current.angular_velocity))
                return 2;
            const auto stored = world.object(root.id).value();
            result = {stored.velocity, stored.angular_velocity, stored.acceleration, stored.rotation};
        }
        network::Value bits;
        bits.kind = network::Value::Kind::Array;
        for (float value : {result.velocity.x, result.velocity.y, result.velocity.z, result.angular_velocity.x,
                            result.angular_velocity.y, result.angular_velocity.z, result.acceleration.x,
                            result.acceleration.y, result.acceleration.z, result.orientation.x, result.orientation.y,
                            result.orientation.z, result.orientation.w})
            bits.array.emplace_back(std::to_string(std::bit_cast<std::uint32_t>(value)));
        std::cout << network::osd_json(bits, true) << '\n';
    }
}

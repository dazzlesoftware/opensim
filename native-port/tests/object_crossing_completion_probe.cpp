#include "opensim/scene/world.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <iostream>
using namespace opensim;

network::Value vector(framework::Vector3 v) {
    network::Value a;
    a.kind = network::Value::Kind::Array;
    for (float x : {v.x, v.y, v.z})
        a.array.emplace_back(std::to_string(std::bit_cast<std::uint32_t>(x)));
    return a;
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto f = network::parse_osd_json(line);
        scene::World world(framework::Uuid::random(), f.get("width").integer, f.get("height").integer);
        auto root = scene::SceneObject::primitive(framework::Uuid::random(),
                                                  {float(f.get("x").real), float(f.get("y").real), 7});
        root.velocity = {1, 2, 3};
        root.angular_velocity = {4, 5, 6};
        root.acceleration = {7, 8, 9};
        root.move_target = {{12, 13, 14}, f.get("move").boolean ? 1.f : 0.f};
        root.look_at = {{0, 0, 0, 1}, f.get("look").boolean ? .8f : 0.f};
        const auto added = world.add(root, f.get("backup").boolean, false).value();
        auto child = scene::SceneObject::primitive(root.owner, {1, 2, 3});
        child.parent_id = added.local_id;
        world.add(child, f.get("backup").boolean, false);
        world.restore_rejected_crossing(root.id);
        const auto result = world.object(root.id).value();
        std::cout << network::osd_json(
                         network::Value::object(
                             {{"position", vector(result.position)},
                              {"velocity", vector(result.velocity)},
                              {"angular", vector(result.angular_velocity)},
                              {"acceleration", vector(result.acceleration)},
                              {"move", network::Value(result.move_target.active())},
                              {"look", network::Value(result.look_at.active())},
                              {"backup", network::Value(result.attached_to_backup)},
                              {"childOffset", vector(world.object(child.id)->position)},
                              {"updates", network::Value(static_cast<std::int32_t>(world.drain_events().size()))}}),
                         true)
                  << '\n';
    }
}

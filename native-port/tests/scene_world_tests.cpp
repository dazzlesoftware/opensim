// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include <algorithm>
#include "opensim/framework/texture_entry.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace opensim;
namespace {
int checks = 0;
void check(bool okay, const char* name) {
    if (!okay)
        throw std::runtime_error(name);
    ++checks;
}
template <class F> bool throws(F call) {
    try {
        call();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}
float parse_bits(const std::string& text) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(text, nullptr, 16)));
}
void terrain_probe() {
    scene::Terrain terrain(32, 32);
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            terrain.set(x, y, static_cast<float>((x * 17 - y * 11) % 37) * .25f);
    terrain.clear_taint();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        char op;
        in >> op;
        try {
            if (op == 'h') {
                std::string x, y;
                in >> x >> y;
                std::cout << std::hex << bits(terrain.height_at(parse_bits(x), parse_bits(y))) << std::dec;
            } else if (op == 'p') {
                int x, y;
                std::string sub, mul;
                in >> x >> y >> sub >> mul;
                auto values = terrain.patch(x, y, parse_bits(sub), parse_bits(mul));
                for (auto value : values)
                    std::cout << std::hex << bits(value) << ',';
                std::cout << std::dec;
            } else if (op == 'm') {
                int x, y;
                in >> x >> y;
                auto [min, max] = terrain.patch_min_max(x, y);
                std::cout << std::hex << bits(min) << ',' << bits(max) << std::dec;
            } else if (op == 's') {
                int x, y;
                std::string value;
                in >> x >> y >> value;
                terrain.set(x, y, parse_bits(value));
                std::cout << terrain.tainted_count();
            } else if (op == 't') {
                int x, y, clear;
                in >> x >> y >> clear;
                std::cout << terrain.tainted_patch(x, y, clear) << ',' << terrain.tainted_count();
            } else if (op == 'n') {
                int start;
                in >> start;
                std::cout << terrain.next_taint(start) << ',' << terrain.tainted_count();
            } else if (op == 'c') {
                terrain.clear_taint();
                std::cout << terrain.tainted_count();
            } else if (op == 'a') {
                terrain.taint_all();
                std::cout << terrain.tainted_count();
            } else
                throw std::runtime_error("op");
        } catch (...) {
            std::cout << "EX";
        }
        std::cout << '\n';
    }
}
void terrain_tests() {
    scene::Terrain t(32, 16);
    check(t.width() == 32 && t.height() == 16 && t.get(0, 0) == 0 && !t.tainted(),
          "source dimension constructor initializes zero without taints");
    t.set(16, 0, 5);
    check(t.tainted_count() == 1 && t.tainted_patch(1, 0), "changed height taints correct16m patch");
    t.set(16, 0, 5);
    check(t.tainted_count() == 1, "same value does not add taint");
    check(t.next_taint() == 1 && !t.tainted(), "inclusive taint drain clears patch");
    t.clear_land();
    check(t.get(31, 15) == 21 && !t.tainted(), "ClearLand defaults21 and does not taint");
    t.set(0, 0, -0.f);
    t.clear_taint();
    t.set(0, 0, 0.f);
    check(std::signbit(t.get(0, 0)) && !t.tainted(), "equal signed zero preserves stored bit pattern");
    t.clear_land(0);
    t.set(1, 0, 10);
    t.set(0, 1, 20);
    t.set(1, 1, 100);
    check(t.height_at(.25f, .75f) == 35.f && t.height_at(.75f, .25f) == 30.f,
          "height sampling follows source triangles");
    check(t.height_at(-1, .25f) == 5.f && t.height_at(.25f, -1) == 2.5f,
          "height sampling interpolates clamped edges");
    check(t.height_at(100, 100) == 0.f, "height sampling clamps finite outer edge");
    check(throws([&] { t.height_at(std::numeric_limits<float>::quiet_NaN(), 0); }),
          "NaN coordinate preserves source indexing failure");
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 16; ++y)
            t.set(x, y, float(x * 100 + y));
    auto patch = t.patch(1, 0);
    auto floats = t.serialized_floats();
    auto compressed = t.compressed_map();
    check(patch[3 * 16 + 2] == 1803 && floats[3 * 32 + 18] == 1803 && compressed[18 * 16 + 3] == 1803,
          "viewer patches and serialization retain distinct source orderings");
    t.clear_land(0);
    t.set(0, 15, -100);
    t.set(1, 15, 100);
    auto mm = t.patch_min_max(0, 0);
    check(mm.first == 0 && mm.second == 100 && t.patch(0, 0)[15 * 16] == -100,
          "patch minmax retains source first-column omission");
    t.clear_taint();
    t.taint_all();
    auto clone = t.clone();
    check(clone.get(0, 15) == -100 && !clone.tainted() && t.tainted_count() == 2,
          "source Clone copies heights without taints");
    check(throws([&] { t.get(-1, 0); }) && throws([&] { t.patch(2, 0); }),
          "checked native array/patch bounds");
    auto nan = std::numeric_limits<float>::quiet_NaN();
    t.set(0, 0, nan);
    t.clear_taint();
    t.set(0, 0, nan);
    check(t.tainted(), "NaN assignment always marks source change");
}
void world_tests() {
    framework::Uuid owner("00000000-0000-0000-0000-000000000007");
    scene::World world({}, 256, 256, 100);
    auto object = scene::SceneObject::primitive(owner, {128, 128, 25});
    check(!object.id.is_zero() && object.owner == owner && object.creator == owner &&
              object.rezzer == owner && object.last_owner == owner,
          "new primitive source identity/ownership");
    check(object.name == "Object" && object.rotation.w == 1 && !object.texture_entry.empty() &&
              object.shape.pcode == 9 && object.shape.path_scale_x == 100,
          "primitive source name, identity rotation, texture and box shape");
    auto added = world.add(object);
    check(added && added->local_id == 101 && added->changed && added->attached_to_backup,
          "add assigns shared local ID and backup state");
    check(world.allocate_local_id() == 102, "avatar shares same allocator");
    check(!world.add(object) && world.size() == 1, "duplicate UUID rejected before ID allocation");
    check(!world.add(scene::SceneObject{}), "zero UUID rejected");
    check(world.update_transform(object.id, {1, 2, 3}, {0, 0, 0, 1}, {2, 3, 4}) &&
              world.object(101)->position == framework::Vector3{1, 2, 3},
          "transform changes visible through local index");
    check(world.update_motion(object.id, {4, 5, 6}, {1, 2, 3}, {2, 2, 2}), "motion state updates");
    check(world.remove(object.id) && !world.object(101) && !world.remove(object.id),
          "remove clears both indexes and missing remove returns false");
    auto events = world.drain_events();
    check(events.size() == 4 && events[0].kind == scene::WorldEventKind::Added &&
              events[1].kind == scene::WorldEventKind::Updated &&
              events[3].kind == scene::WorldEventKind::Removed && events[0].object.position.x == 128 &&
              events[3].sequence == 4,
          "lifecycle events retain snapshots and order");
    check(world.drain_events().empty(), "events drain once");
    scene::PrimShape shape;
    check(shape.face_count() == 6, "uncut box has six faces");
    shape.profile_begin = 12500;
    shape.profile_hollow = 1500;
    check(shape.face_count() == 8, "cut hollow box replaces a side and adds cut and hollow faces");
    shape.profile_curve = 3;
    shape.profile_begin = 33333;
    check(shape.face_count() == 6, "prism cut crosses two triangular sides");
    shape.profile_curve = 5;
    shape.path_curve = 32;
    shape.path_twist = 1;
    check(shape.face_count() == 6, "sphere counts twist cuts, dimple and hollow faces");
    check(!world.set_selected(object.id, true), "selection ignores missing objects");
    world.add(object, false, false);
    check(world.set_selected(object.id, true) && !world.set_selected(object.id, true),
          "selection reports only the first transition");
    check(world.set_selected(object.id, false) && !world.set_selected(object.id, false),
          "deselection reports only the first transition");
    world.set_selected(object.id, true);
    world.remove(object.id);
    world.add(object, false, false);
    check(world.set_selected(object.id, true), "object removal clears selection before reusing the ID");
    world.drain_events();
    framework::TextureEntry paint(framework::TextureEntry::white_texture);
    check(world.update_texture(object.id, paint.bytes()), "visible prim texture edit is applied");
    auto texture_events = world.drain_events();
    check(texture_events.size() == 1 && texture_events.front().script_changes == 16,
          "texture ID edit reports CHANGED_TEXTURE");
    paint.default_texture->color({0.5f, 1.f, 1.f, 1.f});
    check(world.update_texture(object.id, paint.bytes()), "visible prim color edit is applied");
    texture_events = world.drain_events();
    check(texture_events.size() == 1 && texture_events.front().script_changes == 2,
          "color edit reports CHANGED_COLOR");
    paint.default_texture->material_id(owner);
    check(!world.update_texture(object.id, paint.bytes()), "material-only texture edit is ignored");
    world.drain_events();
    check(world.update_extra_physics(object.id, {2, 1200.f, .1f, .2f, -.5f}).has_value(),
          "custom physics applied");
    world.drain_events();
    check(world.update_material(object.id, 2) == 2, "glass material is applied");
    check(world.object(object.id)->physics == scene::PrimPhysicsProperties{2, 1200.f, .2f, .7f, -.5f},
          "material changes friction and restitution while preserving other properties");
    const auto material_events = world.drain_events();
    check(material_events.size() == 1 && material_events.front().kind == scene::WorldEventKind::Updated &&
              material_events.front().object.material == 2,
          "material change schedules the new material");
    check(world.update_material(object.id, 255) == 2 && world.drain_events().empty(),
          "out-of-range material preserves the current value");
    check(world.update_material(object.id, 2) == 2 && world.drain_events().empty(),
          "repeating a material schedules no redundant object update");
    check(world.update_motion(object.id, {4, 5, 6}, {1, 2, 3}, {2, 2, 2}), "seed independent motion");
    world.drain_events();
    check(world.update_angular_velocity(object.id, {0, 0, 2.5f}), "update angular velocity");
    const auto spinning = world.object(object.id);
    check(spinning && spinning->velocity == framework::Vector3{4, 5, 6} &&
              spinning->acceleration == framework::Vector3{2, 2, 2} &&
              spinning->angular_velocity == framework::Vector3{0, 0, 2.5f},
          "angular velocity preserves linear motion");
    auto spin_events = world.drain_events();
    check(spin_events.size() == 1 && spin_events.front().kind == scene::WorldEventKind::TerseUpdated,
          "changed angular velocity schedules a terse update");
    check(world.update_angular_velocity(object.id, {0, 0, 2.5f}) && world.drain_events().empty(),
          "unchanged angular velocity schedules no update");
    check(world.update_angular_velocity(object.id, {}, true), "stop spinning with a full refresh");
    spin_events = world.drain_events();
    check(spin_events.size() == 1 && spin_events.front().kind == scene::WorldEventKind::Updated &&
              spin_events.front().object.angular_velocity == framework::Vector3{},
          "full refresh contains cleared angular velocity");
    check(world.update_angular_velocity(object.id, {}, true), "refresh an already stopped object");
    spin_events = world.drain_events();
    check(spin_events.size() == 1 && spin_events.front().kind == scene::WorldEventKind::Updated,
          "explicit refresh still schedules a full update when already stopped");
    world.remove(object.id);
    auto first = scene::SceneObject::primitive(owner, {0, 0, 0});
    first.local_id = 42;
    auto second = scene::SceneObject::primitive(owner, {0, 0, 0});
    second.local_id = 42;
    world.add(first, false, false);
    world.add(second, false, false);
    check(world.size() == 2 && world.object(42)->id == second.id,
          "source explicit local-ID collision overwrites local lookup");
    world.remove(first.id);
    check(!world.object(42) && world.object(second.id).has_value(),
          "source removal unconditionally clears collided local index");
    auto attachment = scene::SceneObject::primitive(owner, {0, 0, 0});
    attachment.is_attachment = true;
    attachment.shape.state = 1;
    attachment.parent_id = 9;
    auto a = world.add(attachment, true);
    check(a && a->parent_id == 9 && !a->attached_to_backup && a->changed,
          "attachment root parent and backup state");
    auto earlier_id = attachment;
    auto listed_root = scene::SceneObject::primitive(owner, {10, 20, 30});
    listed_root.sale_type = 2;
    listed_root.sale_price = 123;
    auto root = world.add(listed_root, false);
    check(root.has_value(), "linked root inserted");
    auto child = scene::SceneObject::primitive(owner, {1, 2, 3});
    child.local_id = 7;
    child.parent_id = root->local_id;
    const auto linked = world.add(child);
    check(linked && linked->parent_id == root->local_id && !linked->attached_to_backup,
          "linked child retains parent and has no separate backup registration");
    {
        scene::World bounds_world({}, 16, 16);
        const auto box = bounds_world.add(scene::SceneObject::primitive(owner, {4, 4, 4}, {2, 2, 2}));
        auto offset = scene::SceneObject::primitive(owner, {3, 0, 0}, {2, 4, 2});
        offset.parent_id = box->local_id;
        offset.rotation = {0, 0, .70710678f, .70710678f};
        const auto child_box = bounds_world.add(offset);
        const auto [lower, upper] = bounds_world.bounding_box(child_box->id);
        check(lower.x == -1 && std::abs(lower.y + 1.f) < .00001f && lower.z == -1 &&
                  std::abs(upper.x - 5.f) < .00001f && std::abs(upper.y - 1.f) < .00001f && upper.z == 1,
              "linkset bounds include rotated child in the root coordinate system");
    }
    world.drain_events();
    check(!world.object(root->id)->changed, "root starts clean before child rename");
    check(world.update_object_text(child.id, false, "Renamed child"), "rename linked child");
    const auto rename_events = world.drain_events();
    check(rename_events.size() == 1 && rename_events.front().kind == scene::WorldEventKind::Updated &&
              rename_events.front().object.id == child.id &&
              rename_events.front().object.name == "Renamed child" && world.object(root->id)->changed,
          "child rename schedules update and marks root changed");
    check(world.update_object_text(child.id, false, "Renamed child") && world.drain_events().empty(),
          "same name does not schedule another update");
    auto destination_prim = scene::SceneObject::primitive(owner, {2, 3, 4});
    destination_prim.rezzed_at = std::chrono::system_clock::time_point{std::chrono::seconds{1234}};
    const auto destination = world.add(destination_prim);
    check(destination && world.link_prim(root->id, destination->id), "merge existing linkset");
    world.drain_events();
    const auto extra = world.add(scene::SceneObject::primitive(owner, {2, 3, 5}), false, false);
    check(extra && world.link_prim(extra->id, destination->id), "add part to existing linkset");
    const auto link_events = world.drain_events();
    check(std::any_of(
              link_events.begin(), link_events.end(),
              [&](const auto& event) { return event.object.id == child.id && event.script_changes == 32; }),
          "existing child receives link-change event");
    check(world.delink_child(extra->id, destination->id), "delink extra part");
    check(world.object(extra->id)->attached_to_backup, "delinked child gets its own backup registration");
    check(world.object(extra->id)->rezzed_at == destination_prim.rezzed_at,
          "delinked child inherits the linkset root rez time");
    const auto delink_events = world.drain_events();
    check(std::any_of(
              delink_events.begin(), delink_events.end(),
              [&](const auto& event) { return event.object.id == child.id && event.script_changes == 32; }),
          "remaining child receives delink-change event");
    check(world.update_shape(child.id, child.shape), "edit a linked child shape");
    check(world.object(destination->id)->rezzed_at > destination_prim.rezzed_at &&
              world.object(destination->id)->creation_date == destination_prim.creation_date,
          "child shape edit refreshes root rez time independently of creation time");
    const auto former_root = world.object(root->id);
    check(former_root && former_root->sale_type == 0 && former_root->sale_price == 10 &&
              !former_root->attached_to_backup,
          "linking clears the source root sale listing");
    const auto merged = world.object(child.id);
    check(merged && merged->parent_id == destination->local_id &&
              merged->position == framework::Vector3{9, 19, 29},
          "merge flattens child while retaining world position");
    check(world.delink_root(destination->id), "delink selected root");
    const auto promoted = world.object(root->id);
    const auto retained = world.object(child.id);
    check(promoted && promoted->parent_id == 0 && promoted->position == framework::Vector3{10, 20, 30} &&
              retained && retained->parent_id == promoted->local_id &&
              retained->position == framework::Vector3{1, 2, 3},
          "remaining parts stay linked with their world transforms");
    check(promoted->rezzed_at == world.object(destination->id)->rezzed_at &&
              retained->rezzed_at == promoted->rezzed_at,
          "remaining parts inherit the former root rez time when the root is delinked");
    check(world.remove(root->id) && !world.object(child.id), "root removal removes linked child");
    check(!world.add(child), "missing link parent rejected");
    const auto clean_root = world.add(scene::SceneObject::primitive(owner, {10, 20, 30}), false, false);
    check(clean_root.has_value(), "clean transform root inserted");
    auto moving_child = scene::SceneObject::primitive(owner, {1, 2, 3});
    moving_child.parent_id = clean_root->local_id;
    check(world.add(moving_child, false, false).has_value(), "clean transform child inserted");
    check(world.patch_transform(moving_child.id, framework::Vector3{2, 2, 3}, std::nullopt, std::nullopt) &&
              world.object(clean_root->id)->changed,
          "child transform marks the linkset root changed");
    const auto restored_root = world.add(scene::SceneObject::primitive(owner, {}), false, false);
    check(restored_root.has_value(), "restored linkset root");
    auto restored_first = scene::SceneObject::primitive(owner, {1, 0, 0});
    restored_first.parent_id = restored_root->local_id;
    restored_first.link_order = 1000;
    auto restored_second = scene::SceneObject::primitive(owner, {2, 0, 0});
    restored_second.parent_id = restored_root->local_id;
    restored_second.link_order = 1001;
    check(world.add(restored_second, false, false) && world.add(restored_first, false, false),
          "restore children in storage order");
    const auto appended = world.add(scene::SceneObject::primitive(owner, {3, 0, 0}), false, false);
    check(appended && world.link_prim(appended->id, restored_root->id) &&
              world.link_key(restored_root->id, 2) == restored_first.id &&
              world.link_key(restored_root->id, 3) == restored_second.id &&
              world.link_key(restored_root->id, 4) == appended->id,
          "restored link order survives insertion and later linking");
    const auto temporary = world.add(scene::SceneObject::primitive(owner, {}));
    check(temporary.has_value(), "temporary root inserted");
    auto temporary_child = scene::SceneObject::primitive(owner, {1, 0, 0});
    temporary_child.parent_id = temporary->local_id;
    check(world.add(temporary_child).has_value(), "temporary child inserted");
    const auto before_expiry = std::chrono::steady_clock::now();
    world.set_group_flags(temporary->id, false, true, false);
    world.expire_temporary_objects(before_expiry + std::chrono::seconds(59));
    check(world.object(temporary->id).has_value(), "temporary group survives before its deadline");
    world.expire_temporary_objects(before_expiry + std::chrono::seconds(61));
    check(!world.object(temporary->id) && !world.object(temporary_child.id),
          "temporary expiry removes whole linkset");
    earlier_id.id = framework::Uuid("00000000-0000-0000-0000-000000000001");
    check(world.add(earlier_id, false).has_value(), "second attachment");
    const auto worn = world.attachments(9);
    check(worn.size() == 2 && worn[0].id == attachment.id && worn[1].id == earlier_id.id,
          "attachments preserve insertion order");
    check(world.remove(attachment.id) && world.attachments(9).size() == 1,
          "removed attachment leaves wearer index");
    scene::World wrapping({}, 16, 16, 0xffffffffu);
    check(wrapping.allocate_local_id() == 0 && wrapping.allocate_local_id() == 1,
          "unchecked uint32 local-ID wrap includes zero");
    scene::World event_world;
    auto event_object = scene::SceneObject::primitive(owner, {0, 0, 0});
    event_object.flags = 8;
    event_world.add(event_object, false, false);
    event_world.set_inventory_state(event_object.id, false, true, true, 0x10280u);
    check(event_world.object(event_object.id)->flags == 0x102c8u,
          "source local touch/money/drop flags preserve unrelated flags and scripted state");
    event_world.set_inventory_state(event_object.id, true, false, false, 0u);
    check(event_world.object(event_object.id)->flags == 8 &&
              event_world.object(event_object.id)->inventory_empty,
          "clearing script registrations clears only source local flags");
    scene::World threaded;
    std::mutex ids_mutex;
    std::set<std::uint32_t> ids;
    std::vector<std::thread> threads;
    for (int k = 0; k < 8; ++k)
        threads.emplace_back([&] {
            for (int i = 0; i < 1000; ++i) {
                auto id = threaded.allocate_local_id();
                std::lock_guard lock(ids_mutex);
                ids.insert(id);
            }
        });
    for (auto& thread : threads)
        thread.join();
    check(ids.size() == 8000 && *ids.begin() == 720001 && *ids.rbegin() == 728000,
          "concurrent shared allocator produces unique source increment sequence");
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--terrain-probe") {
            terrain_probe();
            return 0;
        }
        terrain_tests();
        world_tests();
        std::cout << "PASS: " << checks << " scene/world foundation checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

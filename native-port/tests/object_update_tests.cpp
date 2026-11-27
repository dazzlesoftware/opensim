#include "opensim/scene/object_update.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/scene/task_inventory_registry.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace opensim;
using namespace scene;

static SceneObject object(int seed, int mode) {
    auto o = SceneObject::primitive(framework::Uuid("11223344-5566-7788-99aa-bbccddeeff00"),
                                    {seed * .25f, -seed * .125f, 23});
    o.id = framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");
    o.local_id = 720001 + seed;
    o.velocity = {seed * .03125f, 0, 1};
    o.acceleration = {1, -2, 0};
    o.angular_velocity = {.5f, 0, -.25f};
    o.rotation = {seed * .01f, -.2f, .3f, (mode & 4) ? -.4f : .4f};
    if (mode & 8)
        o.rotation = {0, 0, 0, 0};
    if (mode & 16)
        o.rotation = {.01f, .02f, .03f, .99995f};
    o.flags = 0x12347;
    if (mode & 32) {
        auto &s = o.shape;
        s.state = seed & 255;
        s.path_curve = 32;
        s.profile_curve = 5;
        s.path_begin = 123;
        s.path_end = 456;
        s.path_scale_x = 115;
        s.path_scale_y = 133;
        s.path_shear_x = 17;
        s.path_shear_y = 255;
        s.path_twist = -123;
        s.path_twist_begin = 127;
        s.path_radius_offset = -17;
        s.path_taper_x = -31;
        s.path_taper_y = 29;
        s.path_revolutions = 33;
        s.path_skew = -44;
        s.profile_begin = 1234;
        s.profile_end = 2345;
        s.profile_hollow = 3333;
    }
    if (mode & 64)
        o.texture_entry = {1, 0, 2, 255, 17};
    if (mode & 128)
        o.texture_entry.clear();
    if (mode & 256) {
        o.scale = {-0.f, 0, -0.f};
        o.position = {-0.f, 0, 0};
        o.velocity = {0, -0.f, 0};
        o.acceleration = {0, 0, -0.f};
        o.angular_velocity = {0, -0.f, 0};
    }
    return o;
}

static ObjectUpdateOptions options(const SceneObject &o, int seed, int mode) {
    ObjectUpdateOptions x;
    x.pseudo_crc = 0xaabb0000u + seed;
    x.material = mode & 32 ? 7 : 3;
    x.click_action = mode & 32 ? 2 : 0;
    if (mode & 2)
        x.generated_client_flags = 0xfff0fff3;
    x.recipient = mode & 1 ? o.owner : framework::Uuid{};
    x.create_selected = mode & 4;
    return x;
}

static void check_terse_filter() {
    ObjectTerseMotion previous{};
    previous.rotation = {0, 0, 0, 1};
    auto current = previous;
    current.position = {.02f, .02f, 0};
    if (object_terse_changed(previous, current, 250.) || !object_terse_changed(previous, current, 251.))
        throw std::runtime_error("terse update did not honor its refresh interval");
    current.position = {.04f, .04f, 0};
    if (!object_terse_changed(previous, current, 100.))
        throw std::runtime_error("stationary position change did not cross the vector tolerance");
    current = previous;
    current.acceleration = {.08f, .08f, 0};
    if (!object_terse_changed(previous, current, 100.))
        throw std::runtime_error("acceleration change did not cross the vector tolerance");

    previous.velocity = {256, 128, 0};
    current = previous;
    current.velocity = {128, 64, 0};
    current.position = {1, 2, 3};
    if (object_terse_changed(previous, current, 100.))
        throw std::runtime_error("equivalent clamped moving velocities triggered a terse update");
    previous.velocity = {.02f, 0, 0};
    current = previous;
    current.velocity = {};
    if (!object_terse_changed(previous, current, 100.))
        throw std::runtime_error("stopping movement did not trigger a terse update");

    previous.velocity = {};
    previous.angular_velocity = {128, 64, 0};
    current = previous;
    current.angular_velocity = {64, 32, 0};
    current.rotation.z = .01f;
    if (object_terse_changed(previous, current, 100.))
        throw std::runtime_error("equivalent clamped angular velocities triggered a terse update");
    previous.angular_velocity = {};
    current.angular_velocity = {};
    if (!object_terse_changed(previous, current, 100.))
        throw std::runtime_error("stationary rotation change did not trigger a terse update");
}

static void check_terse_batches() {
    std::vector<SceneObject> objects;
    for (int seed = 1; seed <= 26; ++seed)
        objects.push_back(object(seed, 0));
    std::vector<const SceneObject *> parts;
    for (const auto &part : objects)
        parts.push_back(&part);
    const auto packets = object_terse_updates(parts, 123);
    if (packets.size() != 2 || packets[0].blocks.at("ObjectData").size() != 25 ||
        packets[1].blocks.at("ObjectData").size() != 1)
        throw std::runtime_error("terse batches did not split at the managed payload limit");
    std::size_t offset = 0;
    for (const auto &packet : packets) {
        if (network::lludp::packet_length(packet) >= 1200)
            throw std::runtime_error("terse batch exceeded the managed payload limit");
        const auto decoded = network::lludp::decode_datagram(network::lludp::encode_datagram(packet));
        for (const auto &row : decoded.blocks.at("ObjectData")) {
            const auto expected = object_terse_update(objects.at(offset++), 123);
            if (row != expected.block("ObjectData"))
                throw std::runtime_error("terse batch changed block order or contents");
        }
    }
    AvatarUpdate avatar;
    avatar.local_id = 0x01020304;
    avatar.state = 7;
    avatar.position = {128, 128, 25};
    avatar.rotation = {.5f, .5f, 0, 0};
    const auto mixed = object_terse_updates(std::span(parts).first(24), 123, {&avatar, 1});
    if (mixed.size() != 2 || mixed[0].blocks.at("ObjectData").size() != 24 ||
        mixed[1].blocks.at("ObjectData").size() != 1)
        throw std::runtime_error("mixed avatar and prim terse packets did not honor block sizes");
    const auto &row = mixed[1].block("ObjectData");
    const auto &data = std::get<std::vector<std::uint8_t>>(row.at("Data"));
    const std::vector<std::uint8_t> prefix{4, 3, 2, 1, 7, 1};
    const std::vector<std::uint8_t> identity_yaw{255, 127, 255, 127, 255, 127, 255, 255};
    if (data.size() != 60 || !std::equal(prefix.begin(), prefix.end(), data.begin()) || data[20] != 128 ||
        data[21] != 63 || !std::equal(identity_yaw.begin(), identity_yaw.end(), data.begin() + 46))
        throw std::runtime_error("avatar terse wire layout or yaw fallback changed");
}

static void check_uniform_linkset_scale() {
    World world(framework::Uuid::random());
    const auto owner = framework::Uuid::random();
    const auto root = world.add(SceneObject::primitive(owner, {128, 128, 30}, {1, 2, 1}));
    if (!root)
        throw std::runtime_error("could not create the resize fixture root");
    auto child = SceneObject::primitive(owner, {3, 1, 0}, {1, .5f, 4});
    child.parent_id = root->local_id;
    if (!world.add(child))
        throw std::runtime_error("could not create the resize fixture child");
    if (world.group_scale_limit(root->id, 10.f, true) != 10.f / (4.f + .001f) ||
        world.group_scale_limit(root->id, .01f, false) != .01f / (.5f - .001f))
        throw std::runtime_error("scale limits must include linked children and source margins");
    if (world.resize_group_uniform(root->id, 3., .01f, 10.f) || world.object(root->id)->scale != root->scale ||
        world.object(child.id)->scale != child.scale)
        throw std::runtime_error("a rejected uniform resize must leave the whole linkset unchanged");
    if (!world.resize_group_uniform(root->id, 2., .01f, 10.f))
        throw std::runtime_error("valid uniform resize was rejected");
    const auto scaled_root = world.object(root->id);
    const auto scaled_child = world.object(child.id);
    if (!scaled_root || !scaled_child || scaled_root->position != root->position ||
        scaled_root->scale != framework::Vector3{2, 4, 2} || scaled_child->scale != framework::Vector3{2, 1, 8} ||
        scaled_child->position != framework::Vector3{6, 2, 0})
        throw std::runtime_error("uniform resize must scale child offsets and every part's dimensions");
}

static void check_root_physics_shape() {
    World world(framework::Uuid::random());
    const auto owner = framework::Uuid::random();
    const auto mesh_asset = framework::Uuid::random();
    const auto geometry = framework::PrimitiveBaseShapeCore::create_mesh(1, mesh_asset, [](const std::string &) {});

    auto mesh = SceneObject::primitive(owner, {128, 128, 25});
    mesh.extra_params = geometry.extra_params();
    mesh.physics.shape = 1;
    const auto mesh_root = world.add(mesh);
    if (!mesh_root || mesh_root->physics.shape != 2)
        throw std::runtime_error("a rezzed mesh root with shape None defaults to Convex Hull");

    PrimPhysicsProperties requested;
    requested.shape = 1;
    const auto mesh_update = world.update_extra_physics(mesh.id, requested);
    if (!mesh_update || mesh_update->shape != 2)
        throw std::runtime_error("a mesh root rejects shape None in favor of Convex Hull");
    requested.shape = 3;
    const auto invalid_update = world.update_extra_physics(mesh.id, requested);
    if (!invalid_update || invalid_update->shape != 2)
        throw std::runtime_error("an invalid mesh shape request uses the mesh default");

    auto prim = SceneObject::primitive(owner, {128, 128, 30});
    prim.physics.shape = 1;
    const auto prim_root = world.add(prim);
    if (!prim_root || prim_root->physics.shape != 0)
        throw std::runtime_error("a rezzed standard root with shape None defaults to Prim");

    for (const bool promote_child : {false, true}) {
        auto child = SceneObject::primitive(owner, {0, 0, 2});
        child.parent_id = prim_root->local_id;
        child.physics.shape = 1;
        child.extra_params = mesh.extra_params;
        const auto linked = world.add(child);
        if (!linked || linked->physics.shape != 1)
            throw std::runtime_error("linked children can retain shape None");
        const bool delinked = promote_child ? world.delink_root(prim.id) : world.delink_child(child.id, prim.id);
        const auto unlinked = world.object(child.id);
        const auto colliders = world.collision_prims();
        const auto collider = std::ranges::find(colliders, child.id, &PhysicalPrim::id);
        if (!delinked || !unlinked || unlinked->parent_id || unlinked->physics.shape != 2 ||
            collider == colliders.end() || !collider->convex_mesh || collider->mesh_asset != mesh_asset)
            throw std::runtime_error("a delinked mesh child gains a convex collider when it becomes a root");
    }
}

static void check_timed_particles() {
    using namespace std::chrono_literals;
    World world(framework::Uuid::random());
    const auto part = world.add(SceneObject::primitive(framework::Uuid::random(), {128, 128, 30}), false);
    if (!part)
        throw std::runtime_error("could not place the particle fixture");
    world.drain_events();
    const auto now = std::chrono::steady_clock::now() + 1h;
    const std::vector<std::uint8_t> particles(86, 42);
    world.set_particle_system(part->id, particles, 0.f, now);
    world.expire_particle_systems(now + 1s);
    if (world.object(part->id)->changed || world.object(part->id)->particle_system != particles)
        throw std::runtime_error("zero-age OSSL particles must remain without marking the prim changed");
    world.drain_events();
    world.set_particle_system(part->id, particles, .5f, now);
    if (world.drain_events().size() != 1)
        throw std::runtime_error("timed particle creation must queue one full update");
    world.expire_particle_systems(now + 590ms);
    if (world.object(part->id)->particle_system != particles)
        throw std::runtime_error("timed particles expired before the source age and grace period");
    world.expire_particle_systems(now + 610ms);
    if (!world.object(part->id)->particle_system.empty() || !world.drain_events().empty())
        throw std::runtime_error("timed particle expiry must clear state without another viewer update");
    world.set_particle_system(part->id, particles, .5f, now);
    world.set_particle_system(part->id, particles, {}, now + 200ms);
    world.expire_particle_systems(now + 1s);
    if (world.object(part->id)->particle_system != particles)
        throw std::runtime_error("a persistent replacement must cancel the old timed emitter");

    world.drain_events();
    world.set_particle_system(part->id, particles, .5f, std::chrono::steady_clock::now() - 1s);
    if (!world.object(part->id)->particle_system.empty() || !world.object(part->local_id)->particle_system.empty() ||
        !world.objects().front().particle_system.empty())
        throw std::runtime_error("viewer and backup reads must omit an expired emitter before the next tick");
    world.drain_events();
    world.set_particle_system(part->id, {});
    if (!world.drain_events().empty())
        throw std::runtime_error("clearing an already expired emitter must not queue a viewer update");
}

static void check_script_derez() {
    using namespace std::chrono_literals;
    using framework::Uuid;
    World world(Uuid::random());
    const auto owner = Uuid::random();
    const auto place = [&](Uuid object_owner, Uuid rezzer = {}, std::uint32_t parent = 0) {
        auto object = SceneObject::primitive(object_owner, {128, 128, 30});
        object.rezzer = rezzer;
        object.parent_id = parent;
        const auto added = world.add(std::move(object));
        if (!added)
            throw std::runtime_error("could not place a script derez fixture");
        return *added;
    };
    const auto root = place(owner);
    const auto host_part = place(owner, {}, root.local_id);
    const auto immediate = place(owner, root.id);
    const auto immediate_child = place(owner, root.id, immediate.local_id);
    const auto temporary = place(owner, root.id);
    const auto foreign = place(Uuid::random(), root.id);
    const auto unrelated = place(owner, Uuid::random());
    const auto child_target = place(owner, root.id, temporary.local_id);
    world.drain_events();

    const auto now = std::chrono::steady_clock::now();
    script::LslHost host;
    host.derez_object = [&](Uuid source, Uuid target, bool later) {
        return world.derez_script_object(source, target, later, now);
    };
    std::string replies;
    host.chat = [&](const script::LslChat &chat) { replies += chat.text; };
    script::LslContext context;
    context.object = host_part.id;
    context.owner = owner;
    const auto call = [](Uuid target, int flag) {
        return "llSay(1,(string)llDerezObject(\"" + target.str() + "\"," + std::to_string(flag) + "));";
    };
    const auto program =
        script::compile_lsl("default{state_entry(){" + call({}, 0) + call(foreign.id, 0) + call(unrelated.id, 0) +
                            call(child_target.id, 0) + call(immediate.id, 0) + call(temporary.id, -1) + "}}");
    if (!program)
        throw std::runtime_error("could not compile the script derez fixture");
    script::LslInstance instance(program.program, context, std::move(host));
    if (instance.run(100).status != script::LslRunStatus::Idle || replies != "000011" || world.object(immediate.id) ||
        world.object(immediate_child.id) || !world.object(foreign.id) || !world.object(unrelated.id) ||
        !world.object(child_target.id))
        throw std::runtime_error("llDerezObject must enforce rezzer, ownership and root identity");
    if (!world.derez_script_object(host_part.id, temporary.id, true, now + 30s))
        throw std::runtime_error("marking an already temporary object must succeed");
    world.expire_temporary_objects(now + 59s);
    if (!world.object(temporary.id) || !(world.object(temporary.id)->flags & 0x20000000u))
        throw std::runtime_error("llDerezObject must preserve the temporary object for one minute");
    world.expire_temporary_objects(now + 60s);
    if (world.object(temporary.id) || world.object(child_target.id))
        throw std::runtime_error("repeated llDerezObject must preserve the original expiry deadline");
}

static void check_object_group_assignment() {
    using framework::Uuid;
    World world(Uuid::random());
    const auto owner = Uuid::random();
    const auto group = Uuid::random();
    const auto root = world.add(SceneObject::primitive(owner, {128, 128, 30})).value();
    auto child_model = SceneObject::primitive(owner, {});
    child_model.parent_id = root.local_id;
    const auto child = world.add(std::move(child_model)).value();
    const auto unrelated = world.add(SceneObject::primitive(owner, {130, 130, 30})).value();
    if (!world.assign_linkset_group(child.id, Uuid::random(), group).empty())
        throw std::runtime_error("A non-owner could assign an object group");
    if (world.assign_linkset_group(child.id, owner, group).size() != 2 || world.object(root.id)->group != group ||
        world.object(child.id)->group != group || !world.object(unrelated.id)->group.is_zero())
        throw std::runtime_error("Assigning the group through a child must update the entire linkset");
    TaskInventoryRegistryHooks hooks;
    hooks.part = [&] { return TaskInventoryPart{child.id, group, child.local_id}; };
    hooks.changed = [](auto, auto) {};
    hooks.aggregate_permissions = hooks.deep_permissions = hooks.clear_scripted = hooks.schedule_full =
        hooks.group_changed = [] {};
    hooks.remove_script = [](auto, auto) {};
    TaskInventoryRegistry inventory(std::move(hooks));
    auto item = std::make_shared<framework::TaskInventoryItem>();
    item->item_id = Uuid::random();
    inventory.restore({item});
    const auto serial = inventory.serial();
    inventory.inventory_changed(false);
    inventory.assign_group(group, true);
    if (item->group_id != group || inventory.serial() != serial + 1 || inventory.inventory_changed())
        throw std::runtime_error("Attachment inventory group update must advance the serial");
    inventory.assign_group({}, false);
    if (!item->group_id.is_zero() || inventory.serial() != serial + 2 || !inventory.inventory_changed())
        throw std::runtime_error("Clearing a rezzed object's inventory group must mark it changed");
}

int main(int argc, char **) {
    try {
        if (argc > 1) {
            int seed, mode;
            while (std::cin >> seed >> mode) {
                auto o = object(seed, mode);
                auto p = object_update(o, 0x123456789abcdef0ull, seed * .03125f, options(o, seed, mode));
                for (auto b : network::lludp::encode_datagram(p))
                    std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
                std::cout << "\n";
            }
            return 0;
        }
        check_timed_particles();
        check_script_derez();
        check_object_group_assignment();
        auto o = object(3, 0);
        auto p = object_update(o, 123, .5f);
        auto &b = p.block("ObjectData");
        if (std::get<std::vector<std::uint8_t>>(b.at("ObjectData")).size() != 60 ||
            std::get<std::uint64_t>(b.at("Material")) != 3 || std::get<std::uint64_t>(b.at("PathCurve")) != 16 ||
            !std::get<std::vector<std::uint8_t>>(b.at("NameValue")).empty())
            throw std::runtime_error("prim fields");
        auto wire = network::lludp::encode_datagram(p);
        auto back = network::lludp::decode_datagram(wire);
        if (std::get<framework::Uuid>(back.block("ObjectData").at("FullID")) != o.id)
            throw std::runtime_error("wire identity");
        for (int mode = 0; mode < 2; mode++) {
            o = object(1, 0);
            if (mode == 0)
                o.is_attachment = true;
            if (mode == 1)
                o.shape.pcode = 1;
            bool threw = false;
            try {
                object_update(o, 0, 1);
            } catch (const std::invalid_argument &) {
                threw = true;
            }
            if (!threw)
                throw std::runtime_error("unsupported object profile");
        }
        for (const auto pcode : {95, 111, 255}) {
            auto plant = object(1, 0);
            plant.shape.pcode = static_cast<std::uint8_t>(pcode);
            plant.shape.state = 7;
            plant.rotation = {0, 0, 0.6f, 0.8f};
            plant.flags = 0x400;
            const auto decoded =
                network::lludp::decode_datagram(network::lludp::encode_datagram(object_update(plant, 123, 1)));
            const auto &row = decoded.block("ObjectData");
            const auto &motion = std::get<std::vector<std::uint8_t>>(row.at("ObjectData"));
            const auto rotation_bytes = std::span(motion).subspan(36, 12);
            const bool has_rotation = std::ranges::any_of(rotation_bytes, [](auto byte) { return byte != 0; });
            if (motion.size() != 60 || has_rotation != (pcode != 95) ||
                std::get<std::uint64_t>(row.at("PCode")) != static_cast<std::uint64_t>(pcode) ||
                std::get<std::uint64_t>(row.at("State")) != 7 ||
                std::get<std::uint64_t>(row.at("UpdateFlags")) != 0x400 ||
                std::get<std::uint64_t>(row.at("Material")) != 0 ||
                std::get<std::uint64_t>(row.at("ClickAction")) != 0 ||
                std::get<std::vector<std::uint8_t>>(row.at("Data")) != std::vector<std::uint8_t>{7} ||
                !std::get<std::vector<std::uint8_t>>(row.at("TextureEntry")).empty())
                throw std::runtime_error("vegetation fields on the wire");
        }
        o = object(1, 0);
        o.parent_id = 42;
        o.texture_animation = {3, 255, 4, 8, 0, 0, 160, 63, 0, 0, 32, 64, 0, 0, 64, 192};
        const auto child = network::lludp::decode_datagram(network::lludp::encode_datagram(object_update(o, 123, 1)));
        const auto &linked = child.block("ObjectData");
        if (std::get<std::uint64_t>(linked.at("ParentID")) != 42 ||
            !std::get<std::vector<std::uint8_t>>(linked.at("NameValue")).empty() ||
            std::get<std::vector<std::uint8_t>>(linked.at("TextureAnim")) != o.texture_animation)
            throw std::runtime_error("linked child fields on the wire");
        ObjectAssetPart asset_part{o, 0, {}};
        asset_part.object.parent_id = 0;
        asset_part.object.creator_data = "https://example.test/;A & B";
        asset_part.object.rotation_axis_locks = 5;
        asset_part.object.flags |= prim_flag_volume_detect;
        asset_part.object.buoyancy = 1.25f;
        asset_part.object.force = {1.f, -2.f, 3.f};
        asset_part.object.torque = {-4.f, 5.f, -6.f};
        asset_part.object.particle_system.assign(86, 42);
        std::u16string asset_xml;
        framework::XmlTextWriterIo io;
        io.encoding = [] { return std::shared_ptr<framework::NativeXmlTextEncoding>{}; };
        io.write = [&](auto, auto text) {
            if (text)
                asset_xml.append(*text);
        };
        io.write_array = [&](auto data, int offset, int count) { asset_xml.append(data->data() + offset, count); };
        io.write_whole_array = [&](auto data) { asset_xml.append(data->data(), data->size()); };
        framework::NativeXmlTextWriter writer(std::move(io));
        auto asset_child = asset_part;
        asset_child.object.id = framework::Uuid::random();
        asset_child.object.buoyancy = -0.25f;
        asset_child.object.force = {7.f, 8.f, 9.f};
        asset_child.object.torque = {};
        write_object_asset_xml(writer, asset_part, std::span(&asset_child, 1));
        const auto utf8 = framework::utf8(asset_xml);
        const auto restored = read_object_asset_xml(std::vector<std::uint8_t>(utf8.begin(), utf8.end()));
        if (!restored || restored->linksets.at(0).parts.at(0).object.creator_data != asset_part.object.creator_data)
            throw std::runtime_error("Object XML lost the creator home URI or display name");
        const std::string rewritten_creator_xml = "<SceneObjectGroup><SceneObjectPart><CreatorData/>"
                                                  "<CreatorData>https://example.test/;Grid Creator</CreatorData>"
                                                  "</SceneObjectPart></SceneObjectGroup>";
        const auto rewritten = read_object_asset_xml(
            std::vector<std::uint8_t>(rewritten_creator_xml.begin(), rewritten_creator_xml.end()));
        if (!rewritten ||
            rewritten->linksets.at(0).parts.at(0).object.creator_data != "https://example.test/;Grid Creator")
            throw std::runtime_error("Object XML ignored creator details appended during Hypergrid export");
        const auto read_part_serial = [](std::string_view fields) {
            const auto xml =
                "<SceneObjectGroup><SceneObjectPart>" + std::string(fields) + "</SceneObjectPart></SceneObjectGroup>";
            const auto asset = read_object_asset_xml(std::vector<std::uint8_t>(xml.begin(), xml.end()));
            return asset ? std::optional{asset->linksets.front().parts.front().inventory_serial} : std::nullopt;
        };
        const auto read_serial = [&](std::string_view field) {
            return read_part_serial("<InventorySerial>" + std::string(field) + "</InventorySerial>");
        };
        if (read_part_serial("<Shape><ExtraParams>not base64</ExtraParams></Shape>") ||
            read_part_serial("<TextureAnimation>AA==<Unexpected/></TextureAnimation>"))
            throw std::runtime_error("object XML accepted invalid binary content");
        for (const auto &[encoded, expected] :
             {std::pair{"AUAB<!--split--><![CDATA[AWsBdgA=]]>", std::vector<std::uint8_t>{1, 64, 1, 1, 'k', 1, 'v', 0}},
              std::pair{"invalid!", std::vector<std::uint8_t>{}}}) {
            const auto xml = std::string("<SceneObjectGroup><SceneObjectPart/><lnkstdt>") + encoded +
                             "</lnkstdt><StartStr>first<![CDATA[ second]]></StartStr></SceneObjectGroup>";
            const auto linkset = read_object_asset_xml(std::vector<std::uint8_t>(xml.begin(), xml.end()));
            if (!linkset || linkset->linksets.front().parts.front().object.linkset_data != expected ||
                linkset->linksets.front().parts.front().object.rez_start_string != "first second")
                throw std::runtime_error("object XML did not restore optional linkset data and rez text");
        }
        if (read_serial("-1") != 0xffffffffu || read_serial("-2147483648") != 0x80000000u ||
            read_serial("2147483647") != 0x7fffffffu || read_serial(" +37 ") != 37u ||
            read_serial("3<![CDATA[7]]>") != 37u || read_serial("3<!--join-->7") != 37u)
            throw std::runtime_error("inventory serial did not follow signed XML integer conversion");
        for (const auto field : {"", "2147483648", "-2147483649", "+-37", "<Nested/>"}) {
            if (read_serial(field))
                throw std::runtime_error("object XML accepted an invalid inventory serial");
        }
        const std::string serial = "<InventorySerial>37</InventorySerial>";
        const std::string item =
            "<TaskInventoryItem><ItemID>f344532c-be95-4577-955a-751ea82bfd7d</ItemID></TaskInventoryItem>";
        const auto inventory = "<TaskInventory>" + item + "</TaskInventory>";
        if (read_part_serial(serial + inventory) != 38u || read_part_serial(inventory + serial) != 37u ||
            read_part_serial(inventory) != 1u ||
            read_part_serial("<InventorySerial>-1</InventorySerial>" + inventory) != 0u)
            throw std::runtime_error("object XML did not apply inventory serial changes in document order");
        if (read_part_serial("<TaskInventory>" + item + item + "</TaskInventory>"))
            throw std::runtime_error("object XML accepted duplicate task inventory item IDs");
        const std::string inventory_xml =
            "<SceneObjectGroup><SceneObjectPart><TaskInventory>"
            "<TaskInventoryItem><Name>First<![CDATA[ second]]></Name><OwnerChanged>TrUe</OwnerChanged>"
            "<AssetID><Guid>f344532c-<!--split-->be95-4577-955a-751ea82bfd7d</Guid></AssetID>"
            "<OwnerID>f344532c-be95-<![CDATA[4577-955a-751ea82bfd7d]]></OwnerID>"
            "<BasePermissions>-1</BasePermissions><CurrentPermissions> -2147483648 </CurrentPermissions>"
            "<NextPermissions>2147483648</NextPermissions><PermsMask>+3<![CDATA[7]]></PermsMask>"
            "</TaskInventoryItem>"
            "<TaskInventoryItem><ItemID>f344532c-be95-4577-955a-751ea82bfd7d</ItemID>"
            "<Description/><OwnerChanged>1</OwnerChanged></TaskInventoryItem>"
            "</TaskInventory></SceneObjectPart></SceneObjectGroup>";
        const auto with_inventory =
            read_object_asset_xml(std::vector<std::uint8_t>(inventory_xml.begin(), inventory_xml.end()));
        if (!with_inventory)
            throw std::runtime_error("object XML rejected task inventory text");
        const auto &items = with_inventory->linksets.front().parts.front().inventory;
        if (items.size() != 2 || items[0].name != "First second" || items[0].description || !items[0].owner_changed ||
            items[1].name || items[1].description != "" || !items[1].owner_changed)
            throw std::runtime_error("task inventory text or ownership marker changed during import");
        if (items[0].base_permissions != 0xffffffffu || items[0].current_permissions != 0x80000000u ||
            items[0].next_permissions != 0x7fffffffu || items[0].permissions_mask != 37)
            throw std::runtime_error("task inventory XML did not apply signed integer conversion");
        if (items[0].asset_id != items[1].item_id || items[0].owner_id != items[1].item_id)
            throw std::runtime_error("task inventory XML did not join UUID text");
        if (!restored || restored->linksets.at(0).parts.at(0).object.texture_animation != o.texture_animation)
            throw std::runtime_error("texture animation did not survive object asset serialization");
        if (restored->linksets.at(0).parts.at(0).object.buoyancy != asset_part.object.buoyancy)
            throw std::runtime_error("buoyancy did not survive object asset serialization");
        if (restored->linksets.at(0).parts.at(0).object.force != asset_part.object.force)
            throw std::runtime_error("force did not survive object asset serialization");
        if (restored->linksets.at(0).parts.at(0).object.torque != asset_part.object.torque)
            throw std::runtime_error("torque did not survive object asset serialization");
        const auto &restored_child = restored->linksets.at(0).parts.at(1).object;
        if (restored_child.buoyancy != asset_part.object.buoyancy || restored_child.force != asset_part.object.force ||
            restored_child.torque != asset_part.object.torque)
            throw std::runtime_error("object XML did not serialize the effective linkset physics values");
        const auto &restored_part = restored->linksets.at(0).parts.at(0).object;
        if (restored_part.rotation_axis_locks != 5 || restored_child.rotation_axis_locks != 0)
            throw std::runtime_error("object XML did not preserve root-only rotation locks");
        const std::string lock_xml = "<SceneObjectGroup><SceneObjectPart><RotationAxisLocks> +5 </RotationAxisLocks>"
                                     "<RotationAxisLocks>-2</RotationAxisLocks><PhysicsShapeType>255</PhysicsShapeType>"
                                     "<PhysicsShapeType>-254</PhysicsShapeType></SceneObjectPart>"
                                     "<OtherParts><SceneObjectPart><PhysicsShapeType> +255 </PhysicsShapeType>"
                                     "</SceneObjectPart></OtherParts></SceneObjectGroup>";
        const auto locked_asset = read_object_asset_xml(std::vector<std::uint8_t>(lock_xml.begin(), lock_xml.end()));
        if (!locked_asset || locked_asset->linksets.front().parts.front().object.rotation_axis_locks != 254)
            throw std::runtime_error("object XML did not apply the signed rotation lock conversion");
        if (locked_asset->linksets.front().parts.front().object.physics.shape != 2 ||
            locked_asset->linksets.front().parts.at(1).object.physics.shape != 0)
            throw std::runtime_error("object XML did not narrow or normalize the physics shape type");
        const std::string mesh_shape =
            "<Shape><ExtraParams>ATAAEQAA<!--split--><![CDATA[APNEUyy+lUV3lVp1Hqgr/X0F]]></ExtraParams></Shape>";
        const std::string legacy_mesh_shape =
            "<Shape><SculptTexture><Guid>f344532c-be95-4577-955a-751ea82bfd7d</Guid></SculptTexture>"
            "<SculptType>5</SculptType><SculptEntry>1</SculptEntry></Shape>";
        const std::string invalid_shape_type = "<PhysicsShapeType>255</PhysicsShapeType>";
        const std::string legacy_light =
            "<SceneObjectGroup><SceneObjectPart><Shape><LightEntry>1</LightEntry>"
            "<LightColorR>1</LightColorR><LightIntensity>1</LightIntensity><LightRadius>4</LightRadius>"
            "</Shape></SceneObjectPart></SceneObjectGroup>";
        const auto light_asset =
            read_object_asset_xml(std::vector<std::uint8_t>(legacy_light.begin(), legacy_light.end()));
        const std::vector<std::uint8_t> light_params{1, 32,  0,  16, 0, 0, 0, 255, 0, 0, 255, 0,
                                                     0, 128, 64, 0,  0, 0, 0, 0,   0, 0, 0};
        if (!light_asset || light_asset->linksets.front().parts.front().object.extra_params != light_params)
            throw std::runtime_error("legacy light XML did not produce the expected light parameter block");
        const std::string legacy_flexi =
            "<SceneObjectGroup><SceneObjectPart><Shape><FlexiEntry>TrUe</FlexiEntry>"
            "<FlexiSoftness>3</FlexiSoftness><FlexiTension>1</FlexiTension><FlexiDrag>2</FlexiDrag>"
            "<FlexiGravity>-1</FlexiGravity><FlexiWind>3</FlexiWind>"
            "<FlexiForceX>1</FlexiForceX><FlexiForceY>-2</FlexiForceY><FlexiForceZ>0<![CDATA[.5]]></FlexiForceZ>"
            "</Shape></SceneObjectPart></SceneObjectGroup>";
        const auto flexi_asset =
            read_object_asset_xml(std::vector<std::uint8_t>(legacy_flexi.begin(), legacy_flexi.end()));
        const std::vector<std::uint8_t> flexi_params{1, 16,  0,  16, 0, 0, 0,   138, 148, 90, 30, 0,
                                                     0, 128, 63, 0,  0, 0, 192, 0,   0,   0,  63};
        if (!flexi_asset || flexi_asset->linksets.front().parts.front().object.extra_params != flexi_params)
            throw std::runtime_error("legacy flexible prim XML did not produce the expected parameter block");
        for (const auto &[fields, expected_shape] :
             {std::pair{mesh_shape + invalid_shape_type, 2}, std::pair{legacy_mesh_shape + invalid_shape_type, 2},
              std::pair{invalid_shape_type + mesh_shape, 0}}) {
            const auto xml = "<SceneObjectGroup><SceneObjectPart>" + fields + "</SceneObjectPart></SceneObjectGroup>";
            const auto mesh_asset = read_object_asset_xml(std::vector<std::uint8_t>(xml.begin(), xml.end()));
            if (!mesh_asset || mesh_asset->linksets.front().parts.front().object.physics.shape != expected_shape)
                throw std::runtime_error("physics shape fallback did not use the shape available in XML order");
        }
        const std::string legacy_booleans =
            "<SceneObjectGroup><SceneObjectPart><AllowedDrop>1</AllowedDrop><Flags>Phantom</Flags>"
            "<PassTouches>Tr<![CDATA[UE]]></PassTouches><PassCollisions>yes</PassCollisions>"
            "<SoundQueueing>1</SoundQueueing></SceneObjectPart></SceneObjectGroup>";
        const auto legacy_part =
            read_object_asset_xml(std::vector<std::uint8_t>(legacy_booleans.begin(), legacy_booleans.end()));
        if (!legacy_part)
            throw std::runtime_error("object XML rejected legacy booleans");
        const auto &interactions = legacy_part->linksets.front().parts.front().object;
        if (!interactions.pass_touches || interactions.pass_collisions || !interactions.sound_queueing ||
            !(interactions.flags & 0x10000u))
            throw std::runtime_error("object XML did not preserve legacy interaction booleans");
        if (utf8.find("<VolumeDetectActive>true</VolumeDetectActive>") == std::string::npos ||
            !(restored_part.flags & prim_flag_volume_detect))
            throw std::runtime_error("volume detection did not survive object XML serialization");
        for (const auto &[fields, enabled] :
             {std::pair{"<Flags>Phantom</Flags><VolumeDetectActive>1</VolumeDetectActive>", true},
              std::pair{"<Flags>JointLP2P</Flags><VolumeDetectActive>false</VolumeDetectActive>", false},
              std::pair{"<VolumeDetectActive>TrUe</VolumeDetectActive><Flags>Phantom</Flags>", false}}) {
            const auto xml =
                std::string("<SceneObjectGroup><SceneObjectPart>") + fields + "</SceneObjectPart></SceneObjectGroup>";
            const auto asset = read_object_asset_xml(std::vector<std::uint8_t>(xml.begin(), xml.end()));
            if (!asset || bool(asset->linksets.front().parts.front().object.flags & prim_flag_volume_detect) != enabled)
                throw std::runtime_error("volume detection did not follow object XML field order");
        }
        const auto particles =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_update(restored_part, 123, 1)));
        if (restored_part.particle_system != asset_part.object.particle_system ||
            std::get<std::vector<std::uint8_t>>(particles.block("ObjectData").at("PSBlock")) !=
                asset_part.object.particle_system)
            throw std::runtime_error("particle system did not survive the object asset and viewer update");
        o = object(1, 0);
        o.is_attachment = true;
        o.parent_id = 42;
        o.shape.state = 0x16;
        const auto attachment =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_update(o, 123, 1)));
        const auto &attached = attachment.block("ObjectData");
        const auto expected_name = "AttachItemID STRING RW SV " + o.id.str();
        if (std::get<std::uint64_t>(attached.at("ParentID")) != 42 ||
            std::get<std::uint64_t>(attached.at("State")) != 0x61 ||
            std::get<std::vector<std::uint8_t>>(attached.at("NameValue")) !=
                std::vector<std::uint8_t>(expected_name.c_str(), expected_name.c_str() + expected_name.size() + 1))
            throw std::runtime_error("attachment fields on the wire");
        const std::vector<std::uint8_t> projection{1, 0x40, 0, 28, 0, 0, 0};
        auto projection_params = projection;
        projection_params.resize(35, 1);
        o.extra_params = projection_params;
        o.extra_params[0] = 2;
        o.extra_params.insert(o.extra_params.end(), {0x20, 0, 16, 0, 0, 0});
        o.extra_params.resize(o.extra_params.size() + 16, 2);
        const auto original_params = o.extra_params;
        ObjectUpdateOptions light_options;
        light_options.disable_facelights = true;
        const auto wire_params = [&](const ObjectUpdateOptions &opts) {
            const auto packet = object_update(o, 123, 1, opts);
            return std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("ExtraParams"));
        };
        if (wire_params(light_options) != projection_params || o.extra_params != original_params ||
            wire_params({}) != original_params)
            throw std::runtime_error("facelight filter removes only the light record from the outgoing update");
        for (const auto point : {5, 6}) {
            o.shape.state = static_cast<std::uint8_t>(point);
            if (wire_params(light_options) != original_params)
                throw std::runtime_error("hand attachment retains its light");
        }
        o.is_attachment = false;
        o.parent_id = 0;
        if (wire_params(light_options) != original_params)
            throw std::runtime_error("world prim retains its light");
        o = object(0, 0);
        o.local_id = 0x12345678;
        o.position = {};
        o.velocity = {128, -128, 0};
        o.acceleration = {0, 1, -1};
        o.rotation = {.5f, -.5f, .5f, -.5f};
        o.angular_velocity = {128, -64, 32};
        const std::vector<std::uint8_t> expected{0x78, 0x56, 0x34, 0x12, 0,    0,    0,    0,    0,    0,    0,
                                                 0,    0,    0,    0,    0,    0,    0,    0xff, 0xff, 0,    0,
                                                 0xff, 0x7f, 0xff, 0x7f, 0xfe, 0x81, 0xff, 0x7d, 0xfe, 0xbf, 0xff,
                                                 0x3f, 0xfe, 0xbf, 0xff, 0x3f, 0xff, 0xff, 0xff, 0x3f, 0xfe, 0x9f};
        const auto terse =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_terse_update(o, 123)));
        const auto &part = terse.block("ObjectData");
        if (terse.spec->name != "ImprovedTerseObjectUpdate" || !terse.reliable || !terse.zerocoded ||
            std::get<std::uint64_t>(terse.block("RegionData").at("RegionHandle")) != 123 ||
            std::get<std::vector<std::uint8_t>>(part.at("Data")) != expected ||
            !std::get<std::vector<std::uint8_t>>(part.at("TextureEntry")).empty())
            throw std::runtime_error("terse prim wire layout and managed quantization");
        o.is_attachment = true;
        o.parent_id = 42;
        o.shape.state = 0x16;
        // Every fragment must carry the current region dilation, including
        // mixed avatar/prim updates and packets started after an MTU split.
        const std::vector<const SceneObject *> many_parts(30, &o);
        const std::vector<AvatarUpdate> avatars(1);
        const std::vector<std::pair<float, std::uint64_t>> dilation_cases{
            {-1.f, 0}, {0.f, 0}, {.25f, 16383}, {.5f, 32767}, {.75f, 49151}, {1.f, 65535}, {2.f, 65535}};
        for (const auto &[dilation, packed] : dilation_cases) {
            auto packets = object_terse_updates(many_parts, 123, avatars, dilation);
            if (packets.size() < 2)
                throw std::runtime_error("Dilation fixture did not split the terse packet");
            packets.push_back(object_terse_update(o, 123, dilation));
            packets.push_back(object_update(o, 123, dilation));
            for (const auto &packet : packets) {
                const auto decoded = network::lludp::decode_datagram(network::lludp::encode_datagram(packet));
                if (std::get<std::uint64_t>(decoded.block("RegionData").at("TimeDilation")) != packed)
                    throw std::runtime_error("Current region dilation differs in an object update packet");
            }
        }
        auto expected_attachment = expected;
        expected_attachment[4] = 0x61;
        const auto terse_attachment =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_terse_update(o, 123)));
        if (std::get<std::vector<std::uint8_t>>(terse_attachment.block("ObjectData").at("Data")) != expected_attachment)
            throw std::runtime_error("terse attachment wire layout");
        check_root_physics_shape();
        check_uniform_linkset_scale();
        check_terse_filter();
        check_terse_batches();
        std::cout << "PASS: prim fields, full and terse wire round trips and unsupported-profile rejection\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

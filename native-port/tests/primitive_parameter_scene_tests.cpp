// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/primitive_parameter_scene.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/script/primitive_parameter_constants.hpp"
#include <bit>
#include <iomanip>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace scene;
using namespace script;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

Uuid id(unsigned value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes.back() = static_cast<std::uint8_t>(value);
    return Uuid::from_bytes(bytes);
}

void run_script(std::string source, LslContext context, LslHost host, std::vector<std::string> &messages) {
    const auto program = compile_lsl(source);
    if (!program)
        throw std::runtime_error("Primitive getter compile: " + program.diagnostics.front().message);
    host.chat = [&](const LslChat &chat) { messages.push_back(chat.text); };
    LslInstance instance(program.program, std::move(context), std::move(host));
    const auto result = instance.run(10000);
    if (result.status != LslRunStatus::Idle)
        throw std::runtime_error("Primitive getter execution: " + result.error);
}

void constant_probe() {
    std::string name;
    while (std::cin >> name) {
        std::vector<std::string> messages;
        run_script("default { state_entry() { llSay(0,(string)" + name + "); } }", {}, {}, messages);
        check(messages.size() == 1, "A primitive constant must produce one integer");
        std::cout << name << "=" << messages.front() << '\n';
    }
}

void material_probe() {
    constexpr const char *fields[] = {"NormRepeatX", "NormRepeatY", "NormOffsetX", "NormOffsetY", "NormRotation",
                                      "SpecRepeatX", "SpecRepeatY", "SpecOffsetX", "SpecOffsetY", "SpecRotation"};
    int seed, mode;
    while (std::cin >> seed >> mode) {
        network::Value input;
        input.kind = network::Value::Kind::Map;
        if (mode != 1) {
            for (int i = 0; i < 10; ++i)
                input.set(fields[i], network::Value((seed - 128) * 123457 + i * 97531));
            for (const auto &[key, number] : {std::pair{"NormMap", 50}, {"SpecMap", 51}}) {
                network::Value value(id(number).str());
                value.kind = network::Value::Kind::Uuid;
                input.set(key, std::move(value));
            }
            network::Value color;
            color.kind = network::Value::Kind::Array;
            for (const auto value : {seed, 255 - seed, seed ^ 0x55, 255})
                color.array.emplace_back(value);
            if (mode == 2)
                color.array.pop_back();
            input.set("SpecColor", std::move(color));
            for (const auto *key : {"SpecExp", "EnvIntensity", "DiffuseAlphaMode", "AlphaMaskCutoff"})
                input.set(key, network::Value(seed + 513));
        }
        const auto value = primitive_parameter_material_from_cache(input);
        std::cout << value->normal_map.str() << '|' << value->specular_map.str() << '|';
        for (const float component :
             {value->normal_repeat_x, value->normal_repeat_y, value->normal_offset_x, value->normal_offset_y,
              value->normal_rotation, value->specular_repeat_x, value->specular_repeat_y, value->specular_offset_x,
              value->specular_offset_y, value->specular_rotation})
            std::cout << std::hex << std::setfill('0') << std::setw(8) << std::bit_cast<std::uint32_t>(component)
                      << ';';
        std::cout << std::dec << '|';
        for (const auto component :
             {value->specular_color[0], value->specular_color[1], value->specular_color[2], value->specular_exponent,
              value->environment_intensity, value->alpha_mode, value->alpha_cutoff})
            std::cout << static_cast<unsigned>(component) << ';';
        std::cout << '\n';
    }
}

void scene_test() {
    World world({}, 16, 16);
    AgentCircuitManager circuits;
    ViewerRegion viewers(world, circuits, [](const auto &, auto) { return true; }, {});
    SceneObject root;
    root.id = id(1);
    root.owner = id(200);
    root.name = "Root";
    root.position = {10, 20, 30};
    const auto root_part = world.add(root);
    check(root_part.has_value(), "Create root fixture");
    SceneObject child;
    child.id = id(2);
    child.owner = root.owner;
    child.name = "Child";
    child.position = {1, 2, 3};
    child.parent_id = root_part->local_id;
    check(world.add(child).has_value(), "Create child fixture");

    const auto add_avatar = [&](int number, const char *name) {
        auto circuit = std::make_shared<AgentCircuitData>();
        circuit->agent_id = id(number);
        circuit->session_id = id(number + 50);
        circuit->first_name = name;
        circuit->last_name = "Avatar";
        circuit->appearance = std::make_shared<AvatarAppearance>();
        check(viewers.add_npc(circuit, root.owner), "Create actual seated NPC presence");
        return circuit;
    };
    const auto first = add_avatar(100, "First");
    const auto second = add_avatar(101, "Second");
    for (const auto &avatar : {second, first})
        check(viewers
                  .update_seat(avatar->agent_id, avatar->session_id,
                               ViewerAgent::Seat{child.id, root_part->local_id, {4, 5, 6}, {0, 0, 0, 1}, false},
                               {14, 25, 36})
                  .has_value(),
              "Seat in a different order from presence insertion");

    LslContext context;
    context.object = child.id;
    context.owner = root.owner;
    context.object_name = child.name;
    LslHost host;
    host.ossl_enabled = true;
    std::vector<std::string> permissions;
    host.ossl_permission_error = [&](Uuid, Uuid, std::string_view function) {
        permissions.emplace_back(function);
        return std::string{};
    };
    host.primitive_parameters = [&](Uuid object, PrimitiveQueryApi api, Uuid target, int link, const LslList &rules,
                                    const LslHost::PrimitiveParameterError &report) {
        PrimitiveParameterSceneServices services;
        services.legacy_sit_offsets = true;
        services.error = report;
        return PrimitiveParameterScene(world, &viewers, std::move(services)).query(object, api, target, link, rules);
    };
    std::vector<std::string> messages;
    run_script(R"LSL(default { state_entry() {
        list own = llGetPrimitiveParams([PRIM_NAME, PRIM_POSITION, PRIM_POS_LOCAL,
            PRIM_LINK_TARGET, LINK_ROOT, PRIM_NAME]);
        list linked = llGetLinkPrimitiveParams(LINK_SET, [PRIM_NAME]);
        list osl = osGetLinkPrimitiveParams(LINK_SET, [PRIM_NAME]);
        list other = osGetPrimitiveParams("00000000-0000-0000-0000-000000000001", [PRIM_NAME]);
        list seated = llGetLinkPrimitiveParams(3, [PRIM_NAME, PRIM_POS_LOCAL, PRIM_POSITION]);
        if (llGetListLength(own) == 4 && llList2String(own,0) == "Child" &&
            llList2Vector(own,1) == <11,22,33> && llList2Vector(own,2) == <1,2,3> &&
            llList2String(own,3) == "Root" &&
            llDumpList2String(linked,"|") == "Root|Child|Second Avatar|First Avatar" &&
            llDumpList2String(osl,"|") == "Root|Child" && llList2String(other,0) == "Root" &&
            llGetListLength(seated) == 3 && llList2String(seated,0) == "Second Avatar" &&
            llList2Vector(seated,1) == <4,5,6> && llList2Vector(seated,2) == <14,25,36>)
            llOwnerSay("queries verified");
        else llOwnerSay("query mismatch");
    } })LSL",
               context, host, messages);
    check(messages == std::vector<std::string>{"queries verified"}, "All four compiled APIs use actual scene state");
    check(permissions == std::vector<std::string>{"osGetLinkPrimitiveParams"},
          "Only the High OSSL getter checks named threat rules");

    PrimitiveParameterScene scene(world, &viewers);
    world.set_script_permission_mask(root.id, 1, 0);
    check(scene.query(child.id, PrimitiveQueryApi::OtherObject, root.id, -4, {{27}}).items.empty(),
          "Foreign-object getter requires root Modify permission even for same owner");

    // The error branch must retain the collected prefix, emit the source debug
    // and shout notification, and pause before the following script statement.
    world.set_script_permission_mask(root.id, 1, 0x9e000);
    messages.clear();
    std::int64_t clock = 0;
    host.clock_ticks = [&] { return clock; };
    bool root_error_position = false;
    host.chat = [&](const LslChat &chat) {
        messages.push_back(chat.text);
        if (chat.type == LslChatType::Debug)
            root_error_position = chat.use_root_position;
    };
    std::vector<LslChatType> listeners;
    host.listener = [&](const LslChat &chat) { listeners.push_back(chat.type); };
    const auto error_program = compile_lsl(R"LSL(default { state_entry() {
        list got = osGetPrimitiveParams("00000000-0000-0000-0000-000000000001",
            [PRIM_NAME, PRIM_LINK_TARGET, <1,2,3>, PRIM_NAME]);
        llOwnerSay(llDumpList2String(got,"|"));
    } })LSL");
    check(bool(error_program), "Compile original partial-result error branch");
    LslInstance error_instance(error_program.program, context, host);
    check(error_instance.run(10000).status == LslRunStatus::Sleeping &&
              messages == std::vector<std::string>{": Error PRIM_LINK_TARGET: parameter must be integer"} &&
              listeners == std::vector<LslChatType>{LslChatType::Shout} && root_error_position,
          "Foreign-object invalid link target emits the original error and yields");
    clock = 10000000;
    check(error_instance.run(10000).status == LslRunStatus::Idle && messages.back() == "Root",
          "Foreign-object error resumes with the actual partial result");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--materials")
            material_probe();
        else if (argc > 1)
            constant_probe();
        else {
            scene_test();
            std::cout << "PASS: compiled primitive getters with live World and ViewerRegion\n";
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

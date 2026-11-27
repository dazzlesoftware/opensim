// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/primitive_parameter_selection.hpp"
#include "opensim/scene/primitive_parameter_values.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace opensim;

namespace {
framework::Uuid id(unsigned value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes.back() = static_cast<std::uint8_t>(value);
    return framework::Uuid::from_bytes(bytes);
}

std::string observe(int parts, int avatars, int seed, int host, int link, int mode) {
    scene::World world({}, 16, 16);
    std::uint32_t parent = 0;
    for (int i = 0; i < parts; ++i) {
        scene::SceneObject part;
        part.id = id(i + 1);
        part.owner = id(200);
        if (i) {
            part.parent_id = parent;
            const int children = parts - 1;
            int rank = (i - 1 + (seed >> 1)) % children;
            if (seed & 1)
                rank = children - rank - 1;
            part.link_order = static_cast<std::uint64_t>(rank + 1);
        }
        const auto inserted = world.add(std::move(part), false, false);
        if (!inserted)
            throw std::runtime_error("Cannot create selector fixture part");
        if (i == 0)
            parent = inserted->local_id;
    }
    const auto selection = static_cast<script::PrimitiveQuerySelection>(mode);
    std::string trace;
    const auto sitting = [&](framework::Uuid root) {
        if (root != id(1))
            throw std::runtime_error("Selector queried a different linkset's avatars");
        trace += selection == script::PrimitiveQuerySelection::Entities && link >= 0 ? "I;" : "S;";
        std::vector<framework::Uuid> result;
        for (int i = 0; i < avatars; ++i)
            result.push_back(id(100 + ((seed & 4) ? avatars - i - 1 : i)));
        return result;
    };
    try {
        const auto selected = scene::select_primitive_parameter_entities(world, sitting, id(host + 1), link, selection);
        std::string result;
        for (const auto entity : selected)
            result += std::to_string(entity.bytes().back()) + ",";
        return result + "|" + trace;
    } catch (const scene::PrimitiveParameterValueError &error) {
        return "!" + error.source_type + ":" + error.what() + "|" + trace;
    }
}

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **) {
    try {
        if (argc > 1) {
            int parts, avatars, seed, host, link, mode;
            while (std::cin >> parts >> avatars >> seed >> host >> link >> mode)
                std::cout << observe(parts, avatars, seed, host, link, mode) << '\n';
            return 0;
        }
        check(observe(4, 2, 1, 2, -1, 0) == "1,2,3,4,|", "LINK_SET retains part membership order");
        check(observe(4, 2, 1, 2, 2, 0) == "4,|", "Numeric links use link order, independently of membership");
        check(observe(4, 2, 0, 2, -1, 1) == "1,2,3,4,100,101,|S;", "LSL link getter appends seated avatars");
        check(observe(4, 2, 0, 2, -1, 2) == "1,2,3,4,|", "Entity LINK_SET excludes seated avatars");
        check(observe(4, 2, 0, 2, -3, 2) == "2,3,4,100,101,|S;", "Entity children selection includes sitters");
        check(observe(4, 2, 4, 2, 5, 1) == "101,|S;", "Avatar numeric links follow sitting order");
        check(observe(1, 0, 0, 3, -4, 2) == "0,|", "Entity LINK_THIS retains the source null entry");
        check(observe(1, 0, 0, 3, -9, 2) == "|", "Unknown negative entity selection does not dereference host");
        check(observe(1, 0, 0, 3, 1, 2).starts_with("!System.NullReferenceException:"),
              "Other entity selectors retain the source missing-host exception");
        std::cout << "PASS: live World primitive parameter selection\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

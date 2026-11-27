// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace opensim;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    check(bool(f), "Missing original object-text fixture");
    return {std::istreambuf_iterator<char>(f), {}};
}

std::string bytes(std::string_view hex) {
    check(hex.size() % 2 == 0, "Malformed original byte hex");
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string out;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        out += static_cast<char>(digit(hex[i]) * 16 + digit(hex[i + 1]));
    return out;
}

bool well_formed(std::u16string_view input) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto c = input[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == input.size() || input[i] < 0xdc00 || input[i] > 0xdfff)
                return false;
        } else if (c >= 0xdc00 && c <= 0xdfff)
            return false;
    }
    return true;
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original object-text fixture directory");
        const auto rows = network::parse_osd_json(read(std::filesystem::path(argv[1]) / "metadata.json"));
        check(rows.array.size() == 123, "Original object-text corpus count differs");
        std::size_t cases = 0, overflows = 0, raw_controls = 0;
        for (const auto &row : rows.array) {
            const auto name = row.get("name").text;
            std::u16string input;
            for (const auto &v : row.get("input").get("units").array)
                input += static_cast<char16_t>(network::osd_integer(v));
            for (const bool description : {false, true}) {
                scene::World world(framework::Uuid::random());
                auto object = scene::SceneObject::primitive(framework::Uuid::random(), {128, 128, 30}, {1, 1, 1});
                object.name = "sentinel-name";
                object.description = "sentinel-desc";
                const auto created = world.add(object);
                check(bool(created), "World fixture object admission failed");
                world.drain_events();
                const auto before = world.object(created->id);
                bool overflow = false;
                try {
                    check(world.update_object_text_utf16(created->id, description, input),
                          name + ": typed World setter rejected");
                } catch (const std::overflow_error &) {
                    overflow = true;
                }
                const bool expected_overflow =
                    !description && row.get("NameException").kind != network::Value::Kind::Undefined;
                check(overflow == expected_overflow, name + ": original overflow category differs");
                if (overflow) {
                    ++overflows;
                    const auto after = world.object(created->id);
                    check(after->name == before->name && after->description == before->description &&
                              world.drain_events().empty(),
                          name + ": overflow partially mutated World metadata/events");
                    continue;
                }
                const auto expected = bytes(row.get(description ? "Desc" : "Name").get("utf8").text);
                const auto after = world.object(created->id);
                check((description ? after->description : after->name) == expected,
                      name + ": typed World getter bytes differ");
                auto events = world.drain_events();
                check(events.size() == 1 && events.front().kind == scene::WorldEventKind::Updated,
                      name + ": metadata change lost update event");
                check(world.update_object_text_normalized(created->id, description, expected),
                      name + ": normalized bridge rejected");
                check(world.drain_events().empty(), name + ": normalized identical value spuriously updated");
                // Apply exact getter bytes from a fresh sentinel to expose non-idempotent
                // NUL padding if the VM bridge accidentally performs the SDK conversion twice.
                check(world.update_object_text_normalized(created->id, description, "sentinel-again"),
                      "Bridge reset failed");
                world.drain_events();
                check(world.update_object_text_normalized(created->id, description, expected), "Bridge update failed");
                const auto bridged = world.object(created->id);
                check((description ? bridged->description : bridged->name) == expected,
                      name + ": bridge normalized getter bytes twice");
                world.drain_events();
                if (well_formed(input)) {
                    ++raw_controls;
                    check(world.update_object_text_normalized(created->id, description, "raw-sentinel"),
                          "Raw reset failed");
                    world.drain_events();
                    check(world.update_object_text(created->id, description, framework::utf8(input)),
                          name + ": ordinary UTF8 entry rejected");
                    const auto raw = world.object(created->id);
                    check((description ? raw->description : raw->name) == expected,
                          name + ": ordinary UTF8 World entry differs");
                }
                ++cases;
            }
        }
        scene::World empty;
        const auto missing = framework::Uuid::random();
        check(!empty.update_object_text_utf16(missing, false, std::u16string{0xd800}) &&
                  !empty.update_object_text(missing, false, "raw") &&
                  !empty.update_object_text_normalized(missing, false, "normalized"),
              "Missing object must refuse before conversion");
        check(cases == 238 && overflows == 8 && raw_controls == 170, "Original World typed corpus accounting differs");
        std::cout << "PASS: 123 actual SDK object-text cases,238 successful typed setters/8 atomic overflow controls, "
                     "normalized bridge and ordinary UTF8 controls; no DB restart claim\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

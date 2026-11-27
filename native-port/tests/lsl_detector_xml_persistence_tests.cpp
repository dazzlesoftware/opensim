// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include <array>
#include <bit>
#include <iostream>
using namespace utf16_test;

namespace {
double number(const network::Value &node) {
    return std::bit_cast<double>(std::stoull(node.get("bits").text, nullptr, 16));
}

framework::Vector3d vector(const network::Value &node) {
    check(node.array.size() == 3, "Original detector vector shape differs");
    return {number(node.array.at(0)), number(node.array.at(1)), number(node.array.at(2))};
}

LslDetector detector(const network::Value &node) {
    LslDetector d;
    d.key = framework::Uuid(node.get("key").text);
    d.owner = framework::Uuid(node.get("owner").text);
    d.group = framework::Uuid(node.get("group").text);
    d.name = node.get("name").text;
    d.type = network::osd_integer(node.get("type"));
    d.link_number = network::osd_integer(node.get("link_number"));
    d.touch_face = network::osd_integer(node.get("touch_face"));
    d.grab = vector(node.get("grab"));
    d.position = vector(node.get("position"));
    d.velocity = vector(node.get("velocity"));
    d.touch_st = vector(node.get("touch_st"));
    d.touch_uv = vector(node.get("touch_uv"));
    d.touch_normal = vector(node.get("touch_normal"));
    d.touch_binormal = vector(node.get("touch_binormal"));
    d.touch_pos = vector(node.get("touch_pos"));
    const auto &q = node.get("rotation").array;
    check(q.size() == 4, "Original detector Rotation shape differs");
    d.rotation = {number(q.at(0)), number(q.at(1)), number(q.at(2)), number(q.at(3))};
    return d;
}

void same_number(double actual, double expected, const std::string &where) {
    check(std::bit_cast<std::uint64_t>(actual) == std::bit_cast<std::uint64_t>(expected), where + ": IEEE bits differ");
}

void same_vector(framework::Vector3d actual, framework::Vector3d expected, const std::string &where) {
    same_number(actual.x, expected.x, where + ".x");
    same_number(actual.y, expected.y, where + ".y");
    same_number(actual.z, expected.z, where + ".z");
}

void compare(const LslDetector &a, const LslDetector &e, const std::string &where) {
    check(a.key == e.key && a.owner == e.owner && a.group == e.group, where + ": UUID changed");
    check(a.name == e.name && a.type == e.type && a.link_number == e.link_number && a.touch_face == e.touch_face,
          where + ": identity/face differs");
    check(lsl_string_code_units(a.sdk_name ? LslValue(LslUtf16String{*a.sdk_name}) : LslValue(a.name)) ==
              lsl_string_code_units(LslValue(e.name)),
          where + ": SDK name differs");
    same_vector(a.grab, e.grab, where + ".grab");
    same_vector(a.position, e.position, where + ".position");
    same_vector(a.velocity, e.velocity, where + ".velocity");
    same_vector(a.touch_st, e.touch_st, where + ".st");
    same_vector(a.touch_uv, e.touch_uv, where + ".uv");
    same_vector(a.touch_normal, e.touch_normal, where + ".normal");
    same_vector(a.touch_binormal, e.touch_binormal, where + ".binormal");
    same_vector(a.touch_pos, e.touch_pos, where + ".touchpos");
    same_number(a.rotation.x, e.rotation.x, where + ".rotation.x");
    same_number(a.rotation.y, e.rotation.y, where + ".rotation.y");
    same_number(a.rotation.z, e.rotation.z, where + ".rotation.z");
    same_number(a.rotation.w, e.rotation.w, where + ".rotation.w");
}

void fixture(const std::filesystem::path &root, const network::Value &row) {
    const auto name = row.get("name").text;
    auto folder = root / name;
    check(row.get("accepted").boolean && row.get("suspended").boolean && row.get("migrated").boolean,
          "Original detector case failed");
    for (const auto *file : {"source.lsl", "original.yobj", "original.il.txt", "snapshot.bin"})
        check(framework::sha256_hex(read(folder / file)) == row.get("sha256").get(file).text,
              "Original portable artifact changed");
    const auto &initial = row.get("initial");
    const auto &loaded = row.get("after_load");
    check(initial.get("detectors").array.size() == 1 && loaded.get("detectors").array.size() == 1,
          "Original active detector count differs");
    auto original = detector(initial.get("detectors").array.at(0));
    auto expected = detector(loaded.get("detectors").array.at(0));
    LslCheckpoint c;
    c.program_hash = row.get("source_hash").text;
    c.state = "default";
    c.active_event = LslEvent{"state_entry", {}, {original}};
    const auto &queued = initial.get("queue").array;
    const auto &saved_queue = loaded.get("queue").array;
    check(queued.size() == 1 && saved_queue.size() == 1, "Original queue count differs");
    c.queued_events = {{queued.at(0).get("name").text, {}, {detector(queued.at(0).get("detectors").array.at(0))}}};
    const auto plain = encode_lsl_checkpoint(c);
    auto internal = decode_lsl_checkpoint(plain);
    check(internal.has_value(), "Internal detector decode failed");
    compare(internal->active_event->detectors.at(0), original, name + " internal active");
    compare(internal->queued_events.at(0).detectors.at(0), c.queued_events.at(0).detectors.at(0),
            name + " internal queue");
    check(encode_lsl_checkpoint(*internal) == plain, "Internal detector bytes changed");
    auto wire = encode_persistent_lsl_checkpoint(c);
    auto external = decode_lsl_checkpoint(wire);
    check(external.has_value(), "Persistent detector decode failed");
    compare(external->active_event->detectors.at(0), expected, name + " persistent active");
    compare(external->queued_events.at(0).detectors.at(0), detector(saved_queue.at(0).get("detectors").array.at(0)),
            name + " persistent queue");
    check(encode_lsl_checkpoint(c) == plain, "Persistent detector writer mutated live state");
    const auto twice = encode_persistent_lsl_checkpoint(*external);
    if (row.get("second_xml_load").kind == network::Value::Kind::Map) {
        const auto second = decode_lsl_checkpoint(twice);
        check(second.has_value(), "Second persistent detector decode failed");
        const auto &oracle = row.get("second_xml_load");
        compare(second->active_event->detectors.at(0), detector(oracle.get("detectors").array.at(0)),
                name + " second persistent active");
        compare(second->queued_events.at(0).detectors.at(0),
                detector(oracle.get("queue").array.at(0).get("detectors").array.at(0)),
                name + " second persistent queue");
    } else {
        check(twice == wire, "Bounded original numeric case lost expected stability");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Expected original numeric and extreme fixture directories");
        const std::array<const char *, 2> hashes{"7822d425db6f2e4a3a2538725cefda2e09e61329a40f0e624a3d316c4599bf77",
                                                 "f0ca8fed3c1ae30ef941e8c8c41cb802a5518929705e48cff06f55f89a8de5d6"};
        for (unsigned corpus = 0; corpus < 2; ++corpus) {
            auto root = std::filesystem::path(argv[corpus + 1]);
            auto text = read(root / "metadata.json");
            check(framework::sha256_hex(text) == hashes[corpus], "Original numeric metadata changed");
            auto rows = network::parse_osd_json(text);
            check(rows.array.size() == (corpus == 0 ? 7 : 5), "Original numeric case count differs");
            for (const auto &row : rows.array)
                fixture(root, row);
        }
        std::cout << "Twelve original detector XML numeric contracts passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

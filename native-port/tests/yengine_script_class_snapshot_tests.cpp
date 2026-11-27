// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <unordered_set>

using namespace utf16_test;

namespace {
using Kind = YEngineManagedValue::Kind;

std::span<const std::uint8_t> bytes(const std::string &data) {
    return {reinterpret_cast<const std::uint8_t *>(data.data()), data.size()};
}

const YEngineManagedArray &array(const YEngineManagedRef &value, std::string_view element) {
    check(value && value->kind == Kind::Array && value->element_type == element,
          "Original managed array storage family differs");
    return std::get<YEngineManagedArray>(value->value);
}

std::int32_t integer(const YEngineManagedRef &value) {
    check(value && value->kind == Kind::Integer, "Original primitive Int32 storage differs");
    return std::get<std::int32_t>(value->value);
}

void original_cases(const std::filesystem::path &root) {
    const auto proof_text = read(root / "verification.json");
    check(framework::sha256_hex(proof_text) == "d8277e63982ff02c77e878ab7a61e45b22c5a97b9c97bc482766720dc4143311",
          "Original script-array proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
          "Original script-array proof scope differs");
    check(framework::sha256_hex(read(root / "generator.cs")) == proof.get("generatorSha256").text,
          "Original script-array generator changed");
    check(framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text,
          "Original script-array independent wire verifier changed");
    unsigned cases = 0;
    for (const auto platform : {"windows", "linux"}) {
        const auto &platform_proof = proof.get("platforms").get(platform);
        check(framework::sha256_hex(read(root / platform / "metadata.json")) ==
                  platform_proof.get("metadataSha256").text,
              "Original platform script-array metadata changed");
        check(platform_proof.get("cases").array.size() == 4, "Missing original script-array source cases");
        for (const auto &row : platform_proof.get("cases").array) {
            const auto folder = root / platform / row.get("name").text;
            for (const auto &[file, field] : std::array<std::pair<const char *, const char *>, 3>{{
                     {"source.lsl", "sourceSha256"},
                     {"metadata.json", "metadataSha256"},
                     {"snapshot.bin", "snapshotSha256"},
                 }})
                check(framework::sha256_hex(read(folder / file)) == row.get(field).text,
                      "Original script-array artifact changed");
            const auto wire = read(folder / "snapshot.bin");
            check(wire.size() == static_cast<std::size_t>(network::osd_integer(row.get("snapshotBytes"))),
                  "Original script-array snapshot size differs");
            const auto snapshot = decode_yengine_snapshot(bytes(wire));
            check(snapshot.has_value() && !snapshot->lsl_projection_complete,
                  "Original script class was rejected or reported as a scalar-only projection");
            const auto &globals = array(snapshot->managed_globals[9], "sc");
            check(!globals.empty(), "Original typed script-class globals missing");
            const auto wrapper = globals.front();
            check(wrapper && wrapper->kind == Kind::ScriptClass, "Generated array wrapper lost its class tag");
            const auto &object = std::get<YEngineManagedScriptClass>(wrapper->value);
            check(object.class_index == 0, "Generated array class index differs");
            for (const auto &alias : globals)
                check(alias == wrapper, "Typed global class aliases split");
            const auto &dimensions = array(object.instance_arrays[3], "in");
            check(dimensions.size() == row.get("dimensions").array.size(), "Generated class dimension count differs");
            for (std::size_t index = 0; index < dimensions.size(); ++index)
                check(integer(dimensions[index]) == network::osd_integer(row.get("dimensions").array[index]),
                      "Public dimension field differs from original");
            const auto &objects = array(object.instance_arrays[5], "ob");
            check(objects.size() == 1, "Generated class backing field inventory differs");
            const auto &cells = array(objects[0], "in");
            check(cells.size() == row.get("backingCells").array.size(), "Backing array length differs");
            for (std::size_t index = 0; index < cells.size(); ++index)
                check(integer(cells[index]) == network::osd_integer(row.get("backingCells").array[index]),
                      "Generated class backing cell differs");
            if (row.get("name").text == "rank1_identity") {
                const auto &object_globals = array(snapshot->managed_globals[5], "ob");
                check(!object_globals.empty() && object_globals[0] == wrapper,
                      "Object and typed global class aliases split");
            }
            ++cases;
        }
    }
    check(cases == 8, "Incomplete original script-class snapshot inventory");
}

void number(std::vector<std::uint8_t> &wire, std::int32_t value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned shift = 0; shift < 32; shift += 8)
        wire.push_back(static_cast<std::uint8_t>(bits >> shift));
}

std::vector<std::uint8_t> cycle_wire(std::uint8_t index_tag = 12, std::int32_t class_index = 0,
                                     bool malformed_lane = false) {
    // Original envelope: version, reserved byte, null arguments, initialization,
    // state/event indices, eleven global arrays, then empty frame-name sentinel.
    std::vector<std::uint8_t> wire{12, 0, 0, 0};
    number(wire, 0);
    number(wire, -1);
    for (unsigned lane = 0; lane < 11; ++lane) {
        if (lane != 9) {
            wire.push_back(0);
            continue;
        }
        wire.insert(wire.end(), {9, 2, 's', 'c'}); // reference 0
        number(wire, 1);
        wire.push_back(19); // reference 1, registered before its instance arrays
        wire.push_back(index_tag);
        if (index_tag == 12 || index_tag == 3)
            number(wire, class_index);
        for (unsigned field = 0; field < 11; ++field) {
            if (field == 5) {
                wire.insert(wire.end(), {9, 2, 'o', 'b'}); // reference 2
                number(wire, 1);
                wire.push_back(15);
                number(wire, 1); // class -> object array -> same class
            } else if (field == 3 && malformed_lane) {
                wire.push_back(12);
                number(wire, 1);
            } else
                wire.push_back(0);
        }
    }
    wire.push_back(0);
    return wire;
}

void class_cycles_and_rejections() {
    std::weak_ptr<const YEngineManagedValue> class_lifetime, backing_lifetime;
    YEngineManagedRef external;
    {
        const auto snapshot = decode_yengine_snapshot(cycle_wire());
        check(snapshot.has_value(), "Valid script-class self-cycle wire rejected");
        external = array(snapshot->managed_globals[9], "sc")[0];
        const auto &object = std::get<YEngineManagedScriptClass>(external->value);
        const auto backing = object.instance_arrays[5];
        check(array(backing, "ob")[0] == external, "Class was not preregistered before reading its self-reference");
        class_lifetime = external;
        backing_lifetime = backing;
    }
    collect_yengine_managed_cycles();
    check(!class_lifetime.expired() && !backing_lifetime.expired() && external->kind == Kind::ScriptClass,
          "Collection discarded an externally rooted class graph");
    check(array(std::get<YEngineManagedScriptClass>(external->value).instance_arrays[5], "ob")[0] == external,
          "Collection cleared a reachable class edge");
    external.reset();
    collect_yengine_managed_cycles();
    check(class_lifetime.expired() && backing_lifetime.expired(), "Unreachable class/array cycle leaked");

    check(!decode_yengine_snapshot(cycle_wire(0)), "Null class index accepted");
    check(!decode_yengine_snapshot(cycle_wire(3)), "Boxed LSLInteger accepted as CLR class index");
    check(!decode_yengine_snapshot(cycle_wire(12, -1)), "Negative class index accepted");
    check(!decode_yengine_snapshot(cycle_wire(12, 0, true)), "Scalar instance-array lane accepted");
    const auto wire = cycle_wire();
    for (std::size_t length = 0; length < wire.size(); ++length)
        check(!decode_yengine_snapshot(std::span(wire).first(length)), "Truncated script-class wire accepted");
    auto trailing = wire;
    trailing.push_back(0);
    check(!decode_yengine_snapshot(trailing), "Trailing script-class wire accepted");
    collect_yengine_managed_cycles();
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original script-array fixture directory");
        original_cases(argv[1]);
        class_cycles_and_rejections();
        std::cout << "PASS 8 original script-class migration snapshots, backing/dimension fields, aliases, "
                     "class cycles and malformed payloads; source compiler support remains separate\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

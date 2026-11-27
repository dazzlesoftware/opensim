// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/network/osd_json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace script;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Could not read original managed fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

struct Coverage {
    bool null_slot = false, boolean = false, character = false, event_code = false, cross_global_frame_alias = false;
    std::size_t references = 0, frames = 0;
};

class GraphComparison {
    std::map<int, std::pair<YEngineManagedRef, std::string>> references_;
    Coverage &coverage_;

  public:
    explicit GraphComparison(Coverage &coverage) : coverage_(coverage) {}

    void compare(const YEngineManagedRef &actual, const network::Value &expected, const std::string &path) {
        check(bool(actual), path + ": missing raw node");
        const auto kind = network::osd_integer(expected.get("kind"));
        if (kind == 15) {
            const auto reference = references_.find(network::osd_integer(expected.get("reference")));
            check(reference != references_.end() && actual == reference->second.first,
                  path + ": original object reference alias was not retained");
            ++coverage_.references;
            if (path.starts_with("frame") && reference->second.second.starts_with("global"))
                coverage_.cross_global_frame_alias = true;
            return;
        }
        check(static_cast<int>(actual->kind) == kind, path + ": original wire scalar/object kind changed");
        if (expected.map.contains("id")) {
            const auto id = network::osd_integer(expected.get("id"));
            check(references_.emplace(id, std::pair{actual, path}).second, path + ": duplicate reference definition");
        }
        const auto &value = expected.get("value");
        switch (kind) {
        case 0:
            check(std::holds_alternative<std::monostate>(actual->value),
                  path + ": null slot was fabricated into a value");
            coverage_.null_slot = true;
            break;
        case 1:
            coverage_.event_code = true;
            [[fallthrough]];
        case 3:
        case 12:
            check(std::get<std::int32_t>(actual->value) == network::osd_integer(value),
                  path + ": integer/event scalar differs");
            break;
        case 2:
        case 10:
            check(std::get<double>(actual->value) == network::osd_real(value), path + ": double scalar differs");
            break;
        case 11:
            check(std::get<float>(actual->value) == static_cast<float>(network::osd_real(value)),
                  path + ": float scalar differs");
            break;
        case 13:
            check(std::get<std::string>(actual->value) == value.text, path + ": string scalar differs");
            break;
        case 16:
            check(std::get<bool>(actual->value) == value.boolean, path + ": boolean scalar differs");
            coverage_.boolean = true;
            break;
        case 20:
            check(std::get<char16_t>(actual->value) == static_cast<char16_t>(network::osd_integer(value)),
                  path + ": UTF-16 character scalar differs");
            coverage_.character = true;
            break;
        case 6: {
            const auto rotation = std::get<LslRotation>(actual->value);
            check(value.array.size() == 4 &&
                      rotation == LslRotation{network::osd_real(value.array[0]), network::osd_real(value.array[1]),
                                              network::osd_real(value.array[2]), network::osd_real(value.array[3])},
                  path + ": rotation differs");
            break;
        }
        case 8: {
            const auto vector = std::get<Vector3d>(actual->value);
            check(value.array.size() == 3 &&
                      vector == Vector3d{network::osd_real(value.array[0]), network::osd_real(value.array[1]),
                                         network::osd_real(value.array[2])},
                  path + ": vector differs");
            break;
        }
        case 9: {
            check(actual->element_type == expected.get("element_type").text,
                  path + ": managed array element type changed");
            const auto &children = std::get<YEngineManagedArray>(actual->value);
            const auto &wanted = expected.get("children").array;
            check(children.size() == wanted.size(), path + ": managed array length changed");
            for (std::size_t index = 0; index < children.size(); ++index)
                compare(children[index], wanted[index], path + "/" + std::to_string(index));
            break;
        }
        case 4:
        case 5:
        case 7: {
            const auto &children = expected.get("children").array;
            check(children.size() == 1, path + ": original wrapper metadata is incomplete");
            compare(std::get<YEngineManagedRef>(actual->value), children.front(), path + "/wrapped");
            break;
        }
        case 17:
            break; // Original current-instance marker has no portable CLR payload.
        default:
            throw std::runtime_error(path + ": unexpected original fixture node kind " + std::to_string(kind));
        }
    }
};

std::vector<std::uint8_t> unhex(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t offset = 0; offset < text.size(); offset += 2)
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(offset, 2)), nullptr, 16)));
    return bytes;
}

YEngineSnapshot old_idle() {
    const auto bytes = unhex("0c10000001000000ffffffff090278610000000009026368000000000902646f000000000902696e"
                             "010000000c2a00000009026c690000000009026f62000000000902726f000000000902737400000000"
                             "09027665000000000902736300000000090464655b5d0000000000");
    const auto decoded = decode_yengine_snapshot(bytes);
    check(decoded && decoded->state_index == 1 && decoded->event_code == -1 && decoded->managed_frames.empty() &&
              decoded->globals[3].size() == 1 && std::get<std::int32_t>(decoded->globals[3][0]) == 42,
          "Original frame-free idle snapshot projection changed");
    return *decoded;
}

void no_replay_guard(const std::vector<YEngineManagedFrame> &frames) {
    const std::string source = R"LSL(integer counter=1;
        default {state_entry(){llSay(0,"startup");}}
        state zed {timer(){llSay(0,(string)counter);}}
        state alpha {timer(){llSay(0,"wrong");}})LSL";
    constexpr std::string_view hash = "9403F8233FCB2421F4E7983DA100DCC2";
    check(yengine_source_matches(source, hash), "No-replay guard source hash does not match the original idle oracle");
    const auto program = compile_lsl(source);
    check(bool(program), "No-replay guard fixture failed to compile");
    unsigned chats = 0;
    LslHost host;
    const std::int64_t clock = 10000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.chat = [&](const auto &) { ++chats; };
    LslInstance target(program.program, {}, host);
    check(target.run(1000).status == LslRunStatus::Idle && chats == 1,
          "Guard target did not reach its live initial state");
    const auto initial = encode_lsl_checkpoint(*target.checkpoint());
    auto unsupported = old_idle();
    unsupported.managed_frames = frames;
    check(!frames.empty() && !target.restore_yengine_snapshot(unsupported, hash, source),
          "Untranslated managed call frames were replayed as a native event");
    check(encode_lsl_checkpoint(*target.checkpoint()) == initial && chats == 1,
          "Rejected managed call frames mutated the existing VM or replayed script side effects");
    unsupported.managed_frames.clear();
    unsupported.lsl_projection_complete = false;
    check(!target.restore_yengine_snapshot(unsupported, hash, source) &&
              encode_lsl_checkpoint(*target.checkpoint()) == initial && chats == 1,
          "Incomplete managed-value projection was restored or mutated the existing VM");
}

void fixture(const std::filesystem::path &directory, Coverage &coverage) {
    const auto wire = read(directory / "snapshot.bin");
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t *>(wire.data()), wire.size());
    const auto metadata = network::parse_osd_json(read(directory / "metadata.json"));
    const auto decoded = decode_yengine_snapshot(bytes);
    check(bool(decoded), "Original suspended managed snapshot did not decode: " + directory.string());
    check(decoded->state_index == static_cast<std::size_t>(network::osd_integer(metadata.get("state_index"))) &&
              decoded->event_code == network::osd_integer(metadata.get("event_code")) &&
              decoded->initialize_globals == metadata.get("initialize_globals").boolean,
          "Original managed snapshot header changed: " + directory.string());
    GraphComparison graph(coverage);
    graph.compare(decoded->managed_event_arguments, metadata.get("event_arguments"), "event");
    check(metadata.get("globals").array.size() == 11, "Original metadata omitted global array banks");
    for (std::size_t index = 0; index < 11; ++index)
        graph.compare(decoded->managed_globals[index], metadata.get("globals").array[index],
                      "global" + std::to_string(index));
    const auto &frames = metadata.get("frames").array;
    check(!frames.empty() && decoded->managed_frames.size() == frames.size(), "Original managed frame count changed");
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto &frame = decoded->managed_frames[index];
        check(frame.function == frames[index].get("function").text &&
                  frame.call_no == network::osd_integer(frames[index].get("call_no")),
              "Original outer-to-inner function/callNo order changed");
        graph.compare(frame.slots, frames[index].get("slots"), "frame" + std::to_string(index));
        ++coverage.frames;
    }
    no_replay_guard(decoded->managed_frames);
    for (std::size_t length = 0; length < bytes.size(); ++length)
        check(!decode_yengine_snapshot(bytes.first(length)), "Truncated original managed snapshot was accepted");
    auto trailing = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    trailing.push_back(127);
    check(!decode_yengine_snapshot(trailing), "Trailing managed snapshot data was ignored");
}

void malformed() {
    for (const auto &bytes : std::vector<std::vector<std::uint8_t>>{
             {12, 16, 15, 255, 255, 255, 255},                     // Unknown reference.
             {12, 16, 9, 2, 'o', 'b', 1, 0, 0, 0, 15, 0, 0, 0, 0}, // In-progress self reference.
             {12, 16, 9, 2, 'o', 'b', 1, 0, 1, 0},                 // More than 65536 array entries.
             {12, 16, 13, 255, 255, 255, 255, 15}})                // Invalid BinaryReader string length.
        check(!decode_yengine_snapshot(bytes), "Malformed managed graph reference or bounds were accepted");
    std::vector<std::uint8_t> deep{12, 16};
    for (int index = 0; index < 65; ++index)
        deep.insert(deep.end(), {9, 2, 'o', 'b', 1, 0, 0, 0});
    deep.push_back(0);
    check(!decode_yengine_snapshot(deep), "Managed nested-array depth bound was not enforced");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original managed fixture directory required");
        old_idle();
        Coverage coverage;
        for (const auto &directory : std::filesystem::directory_iterator(argv[1]))
            if (directory.is_directory() && std::filesystem::is_regular_file(directory.path() / "snapshot.bin"))
                fixture(directory.path(), coverage);
        check(coverage.frames >= 4 && coverage.null_slot && coverage.boolean && coverage.character &&
                  coverage.event_code && coverage.references && coverage.cross_global_frame_alias,
              "Original managed fixtures did not cover frames, nulls, CLR scalars and cross-global/frame aliases");
        malformed();
        std::cout << "PASS: original managed frames, typed raw values, aliases, bounds and no-replay restore guard\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

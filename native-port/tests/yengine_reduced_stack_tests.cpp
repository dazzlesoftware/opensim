// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <stdexcept>

using namespace opensim;
using namespace script;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Missing original reduced-stack fixture: " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

auto bytes(const std::string &value) {
    return std::span(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
}

std::map<std::string, std::size_t> global_indices(const std::string &source) {
    // Read only declarations before the first function parameter list. Saved
    // original slot contracts remain independently checked from original.yobj.
    const auto prefix = source.substr(0, source.find('('));
    const std::regex declaration(R"(\binteger\s+([A-Za-z_][A-Za-z_0-9]*)\s*;)");
    std::map<std::string, std::size_t> result;
    std::size_t index = 0;
    for (auto match = std::sregex_iterator(prefix.begin(), prefix.end(), declaration); match != std::sregex_iterator();
         ++match)
        result.emplace((*match)[1].str(), index++);
    return result;
}

void compare_globals(const LslCheckpoint &checkpoint, const network::Value &expected,
                     const std::map<std::string, std::size_t> &indices) {
    check(checkpoint.globals.size() == indices.size() && expected.map.size() == indices.size(),
          "Original/native reduced-stack globals differ in size");
    for (const auto &[name, index] : indices) {
        check(expected.map.contains(name) && std::holds_alternative<std::int32_t>(checkpoint.globals.at(index)) &&
                  std::get<std::int32_t>(checkpoint.globals.at(index)) == network::osd_integer(expected.get(name)),
              "Original/native reduced-stack global differs: " + name);
    }
}

void resume(LslInstance &instance, const network::Value &oracle, const std::map<std::string, std::size_t> &indices) {
    const bool completes = network::osd_boolean(oracle.get("completed"));
    for (int step = 0; step < 10000; ++step) {
        const auto before = instance.checkpoint();
        check(bool(before), "Reduced-stack VM lost continuation before its expected result");
        check(std::get<std::int32_t>(before->globals.at(indices.at("entries"))) ==
                      network::osd_integer(oracle.get("finalGlobals").get("entries")) &&
                  std::get<std::int32_t>(before->globals.at(indices.at("calls"))) ==
                      network::osd_integer(oracle.get("finalGlobals").get("calls")),
              "Restored deep frames replayed original entry or recursive calls");
        const auto progress = instance.run(1);
        check(progress.status != LslRunStatus::Sleeping, "Original loop continuation unexpectedly slept");
        if (progress.status == LslRunStatus::Faulted) {
            check(!completes && oracle.get("resumeError").text.find("OutOfStackException") != std::string::npos &&
                      progress.error.find("OutOfStackException") != std::string::npos,
                  "Reduced-stack continuation raised an unexpected fault: " + progress.error);
            // Faulted VMs deliberately expose no new checkpoint. The last
            // instruction-boundary globals equal the original fault oracle;
            // the faulting new function's prologue performs no global stores.
            compare_globals(*before, oracle.get("finalGlobals"), indices);
            return;
        }
        if (progress.status == LslRunStatus::Idle) {
            check(completes, "Original overflow case unexpectedly completed after reduced-stack restore");
            compare_globals(*instance.checkpoint(), oracle.get("finalGlobals"), indices);
            return;
        }
    }
    throw std::runtime_error("Reduced-stack continuation exceeded its instruction bound");
}

void exercise(const std::filesystem::path &directory) {
    const auto source = read(directory / "source.lsl");
    const auto raw = read(directory / "snapshot.bin");
    const auto object_bytes = read(directory / "original.yobj");
    const auto metadata = network::parse_osd_json(read(directory / "metadata.json"));
    for (const auto *file : {"source.lsl", "snapshot.bin", "original.yobj", "original.il.txt", "cache_key.txt"})
        check(framework::sha256_hex(read(directory / file)) == metadata.get("sha256").get(file).text,
              "Original reduced-stack fixture provenance changed: " + std::string(file));
    const auto snapshot = decode_yengine_snapshot(bytes(raw));
    const auto object = decode_yengine_object_code(bytes(object_bytes));
    check(snapshot && object && object->source_hash == metadata.get("source_hash").text &&
              snapshot->managed_frames.size() == metadata.get("capturedFrames").array.size(),
          "Original reduced-stack snapshot/object metadata do not match");
    for (std::size_t i = 0; i < snapshot->managed_frames.size(); ++i) {
        const auto &frame = snapshot->managed_frames[i];
        const auto &expected = metadata.get("capturedFrames").array[i];
        check(frame.function == expected.get("function").text &&
                  frame.call_no == network::osd_integer(expected.get("call_no")) && frame.slots &&
                  std::holds_alternative<YEngineManagedArray>(frame.slots->value) &&
                  std::get<YEngineManagedArray>(frame.slots->value).size() ==
                      static_cast<std::size_t>(network::osd_integer(expected.get("slots"))),
              "Original captured frame order/call labels/slot counts changed");
    }
    const auto program = compile_lsl(source);
    check(bool(program), "Original reduced-stack script no longer compiles natively");
    const auto indices = global_indices(source);
    LslHost large_host;
    large_host.clock_ticks = large_host.monotonic_ticks = [] { return std::int64_t{1000000000}; };
    large_host.stack_limit_bytes = 65536;
    LslInstance imported_large(program.program, {}, large_host);
    check(imported_large.restore_yengine_snapshot(*snapshot, object->source_hash, source, *object),
          "64K control could not import unchanged original captured frames");
    const auto native = imported_large.checkpoint();
    check(native && !native->function.empty() && !native->call_stack.empty(),
          "Original deep captured frames did not produce a suspended native continuation");
    compare_globals(*native, metadata.get("capturedGlobals"), indices);
    const auto checkpoint_bytes = encode_lsl_checkpoint(*native);
    const auto decoded = decode_lsl_checkpoint(checkpoint_bytes);
    check(decoded && encode_lsl_checkpoint(*decoded) == checkpoint_bytes,
          "Original translated native checkpoint did not survive its real codec");
    for (const auto &oracle : metadata.get("resumes").array) {
        const auto stack_size = network::osd_integer(oracle.get("stack_size"));
        check(network::osd_boolean(oracle.get("loaded")), "Original oracle did not load its own saved frames");
        LslHost host = large_host;
        host.stack_limit_bytes = stack_size;
        LslInstance managed(program.program, {}, host);
        check(managed.restore_yengine_snapshot(*snapshot, object->source_hash, source, *object),
              "Structurally valid original frames were rejected solely for the destination stack budget");
        compare_globals(*managed.checkpoint(), metadata.get("capturedGlobals"), indices);
        resume(managed, oracle, indices);
        LslInstance restored(program.program, {}, host);
        check(restored.restore_checkpoint(*decoded),
              "Structurally valid native frames were rejected solely for the destination stack budget");
        compare_globals(*restored.checkpoint(), metadata.get("capturedGlobals"), indices);
        resume(restored, oracle, indices);
    }
    LslHost small_host = large_host;
    small_host.stack_limit_bytes = 16384;
    LslInstance preserved(program.program, {}, small_host);
    const auto unchanged = encode_lsl_checkpoint(*preserved.checkpoint());
    auto corrupt = *decoded;
    corrupt.program_hash = "wrong source";
    check(!preserved.restore_checkpoint(corrupt) && encode_lsl_checkpoint(*preserved.checkpoint()) == unchanged,
          "Stack restore change weakened source validation or atomic rejection");
    corrupt = *decoded;
    corrupt.call_stack.front().return_expression = std::numeric_limits<std::size_t>::max();
    check(!preserved.restore_checkpoint(corrupt) && encode_lsl_checkpoint(*preserved.checkpoint()) == unchanged,
          "Stack restore change weakened frame ownership validation");
    small_host.heap_limit_bytes = 1;
    LslInstance constrained(program.program, {}, small_host);
    check(!constrained.restore_checkpoint(*decoded), "Stack restore change bypassed destination heap validation");
    std::cout << "PASS: " << directory.filename().string()
              << " original/native deep frames with original64K/16K outcomes\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original reduced-stack fixture directory required");
        const auto root = std::filesystem::path(argv[1]);
        exercise(root / "unwind_without_new_call");
        exercise(root / "new_complex_call_after_resume");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

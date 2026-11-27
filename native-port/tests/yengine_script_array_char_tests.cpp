// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>

using namespace utf16_test;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

std::vector<std::string> declarations(std::string source) {
    source = global_declaration_source(std::move(source));
    const std::regex declaration(
        R"(\b(?:(?:integer|char)\s*\[\s*\]|integer|char|object|string)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
}

struct GraphComparison {
    bool compare_cells = true;
    std::map<std::int32_t, const void *> references;
    std::map<const void *, std::int32_t> reverse;

    void reference(std::int32_t id, const void *actual, const std::string &where) {
        check(id > 0 && actual, where + ": invalid reference");
        const auto [forward, inserted] = references.emplace(id, actual);
        check(inserted || forward->second == actual, where + ": original aliases split");
        const auto [backward, new_identity] = reverse.emplace(actual, id);
        check(new_identity || backward->second == id, where + ": distinct original references merged");
    }

    void value(const LslValue &actual, const network::Value &expected, const std::string &where) {
        const auto type = expected.get("type").text;
        if (type == "null") {
            check(std::holds_alternative<LslNullObject>(actual) || is_null_lsl_string(actual),
                  where + ": typed null reference differs");
        } else if (type == "integer") {
            const auto *integer = std::get_if<std::int32_t>(&actual);
            check(integer && *integer == network::osd_integer(expected.get("value")), where + ": Int32 differs");
        } else if (type == "char") {
            const auto *character = std::get_if<LslCharacter>(&actual);
            check(character && character->value == network::osd_integer(expected.get("utf16")),
                  where + ": raw UTF16 character differs");
        } else if (type == "string") {
            const auto units = lsl_string_code_units(actual);
            const auto &expected_units = expected.get("utf16").array;
            check(units && units->size() == expected_units.size(), where + ": string length differs");
            for (std::size_t i = 0; i < expected_units.size(); ++i)
                check((*units)[i] == network::osd_integer(expected_units[i]), where + ": UTF16 unit differs");
        } else if (type == "script_class" || type == "script_class_reference") {
            const auto *object = std::get_if<Class>(&actual);
            check(object && *object, where + ": generated class reference missing");
            reference(network::osd_integer(expected.get("reference_id")), object->get(), where);
            if (type == "script_class_reference")
                return;
            const auto &instance = **object;
            check(instance.descriptor && instance.descriptor->canonical_name == expected.get("script_type").text &&
                      instance.descriptor->element_type == expected.get("element_type").text &&
                      instance.descriptor->array_rank == network::osd_integer(expected.get("rank")),
                  where + ": generated type descriptor differs");
            check(instance.descriptor->class_index == network::osd_integer(expected.get("class_index")) &&
                      instance.arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_use")),
                  where + ": generated class index/own heap ledger differs");
            const auto &dimensions = expected.get("integers").array;
            check(instance.instance_arrays[3].size() == dimensions.size(), where + ": dimension count differs");
            for (std::size_t i = 0; i < dimensions.size(); ++i)
                check(std::get<std::int32_t>(instance.instance_arrays[3][i]) == network::osd_integer(dimensions[i]),
                      where + ": public dimension differs");
            const auto &objects = expected.get("objects").array;
            check(instance.instance_arrays[5].size() == objects.size(), where + ": Object field count differs");
            for (std::size_t i = 0; i < objects.size(); ++i)
                value(instance.instance_arrays[5][i], objects[i], where + ".obj");
        } else if (type == "clr_array" || type == "array_reference") {
            const auto *array = std::get_if<Array>(&actual);
            check(array && *array, where + ": raw backing missing");
            reference(network::osd_integer(expected.get("reference_id")), array->get(), where);
            if (type == "array_reference")
                return;
            const auto expected_kind = expected.get("element_type").text == "System.Char" ? LslFixedArrayKind::Character
                                                                                          : LslFixedArrayKind::Integer;
            check((*array)->kind == expected_kind, where + ": raw backing type differs");
            const auto &items = expected.get("items").array;
            check(lsl_fixed_array_size(*array) == items.size(), where + ": backing length differs");
            if (compare_cells)
                for (std::size_t i = 0; i < items.size(); ++i)
                    value(lsl_fixed_array_get(*array, i), items[i], where + "[" + std::to_string(i) + "]");
        } else
            throw std::runtime_error(where + ": unreviewed original observation " + type);
    }
};

void compare(const LslCheckpoint &checkpoint, const std::vector<std::string> &names, const network::Value &expected,
             const std::string &where, bool compare_cells = true) {
    const auto &globals = expected.get("globals");
    check(globals.kind == network::Value::Kind::Map && names.size() == checkpoint.globals.size() &&
              globals.map.size() == names.size(),
          where + ": global inventory differs");
    check(checkpoint.arrays_heap_usage && checkpoint.locals_heap_usage &&
              *checkpoint.arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_used")) &&
              *checkpoint.locals_heap_usage == network::osd_integer(expected.get("locals_heap_used")),
          where + ": global/local heap ledger differs");
    GraphComparison graph;
    graph.compare_cells = compare_cells;
    for (std::size_t i = 0; i < names.size(); ++i)
        graph.value(checkpoint.globals[i], globals.get(names[i]), where + "." + names[i]);
}

std::unique_ptr<LslInstance> restore(std::shared_ptr<const LslProgram> program, const LslHost &host,
                                     const LslCheckpoint &checkpoint, bool persistent) {
    const auto encoded = persistent ? encode_persistent_lsl_checkpoint(checkpoint) : encode_lsl_checkpoint(checkpoint);
    const auto decoded = decode_lsl_checkpoint(encoded);
    auto instance = std::make_unique<LslInstance>(std::move(program), LslContext{}, host);
    check(decoded && instance->restore_checkpoint(*decoded), "Native array checkpoint restore rejected");
    return instance;
}

// This future target is intentionally unregistered until char[] source and
// contextual original imports are implemented. It makes no current pass claim.
LslCheckpoint checkpoint(const LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(result.has_value(), "Character-array checkpoint unavailable");
    return *result;
}

std::string normalized_lines(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    return value;
}

void fault(const std::string &actual, const network::Value &expected) {
    auto type = expected.get("type").text;
    check(type.starts_with("System."), "Unexpected original exception category");
    type.erase(0, 7);
    // Text remains exact apart from the observed Windows/Linux CRLF convention.
    check(normalized_lines(actual) == normalized_lines(type + ": " + expected.get("message").text),
          "Character conversion fault category/text differs: " + actual);
}

void resume(LslInstance &instance, const std::vector<std::string> &names, const network::Value &metadata, bool migrated,
            std::int64_t &clock, unsigned &sleeps, unsigned expected_sleeps) {
    clock += checkpoint(instance).sleep_remaining + 1;
    const auto &expected = metadata.get(migrated ? "final" : "live_resume");
    const auto &expected_fault = metadata.get(migrated ? "resume_exception" : "live_resume_exception");
    auto previous = instance.checkpoint();
    const bool partial = metadata.get("name").text == "char_partial_copy_fault";
    bool finished = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        if (result.status == LslRunStatus::Faulted) {
            check(expected_fault.kind == network::Value::Kind::Map,
                  "Unexpected native character fault: " + result.error);
            fault(result.error, expected_fault);
            check(previous.has_value() && !instance.checkpoint(), "Faulted VM unexpectedly supplied a checkpoint");
            // A faulted VM has no checkpoint API. For the partial-copy case,
            // compare only prior scalar/shape state here; the separate derived
            // inspection control below checks actual mutated backing cells.
            compare(*previous, names, expected, "character state before fault", !partial);
            finished = true;
            break;
        }
        check(result.error.empty() && result.status != LslRunStatus::Sleeping,
              "Character resume faulted or replayed Sleep: " + result.error);
        const auto current = instance.checkpoint();
        check(current.has_value(), "Resume checkpoint missing");
        if (result.status == LslRunStatus::Idle) {
            check(expected_fault.kind != network::Value::Kind::Map, "Original character fault became success");
            compare(*current, names, expected, "character resumed output");
            check(instance.run(1).status == LslRunStatus::Idle, "Completed character event replayed");
            finished = true;
            break;
        }
        previous = current;
    }
    check(finished && sleeps == expected_sleeps, "Character resume exceeded bound/repeated Sleep");
}

void partial_copy_inspection(std::shared_ptr<const LslProgram> program, const LslHost &host,
                             const LslCheckpoint &capture, const std::vector<std::string> &names,
                             const network::Value &metadata, std::int64_t &clock, unsigned &sleeps) {
    auto derived = capture;
    const auto input = std::ranges::find(names, std::string("input"));
    check(input != names.end() && derived.active_event, "Derived partial-copy input/event missing");
    const auto index = static_cast<std::size_t>(input - names.begin());
    // Derived native observation only: source state_entry declares no parameters.
    // An unused event argument exposes the same live class through current_event
    // before the fault. No original snapshot or source is changed or fabricated.
    derived.active_event->arguments = {derived.globals[index]};
    auto observed = restore(program, host, derived, false);
    const auto *event = observed->current_event();
    check(event && event->arguments.size() == 1, "Derived live observer unavailable");
    const auto live_class = std::get<Class>(event->arguments[0]);
    const auto live_backing = std::get<Array>(live_class->instance_arrays[5][0]);
    const auto prior_sleeps = sleeps;
    clock += checkpoint(*observed).sleep_remaining + 1;
    const auto result = observed->run(10000);
    check(result.status == LslRunStatus::Faulted, "Partial-copy derived control did not fault");
    fault(result.error, metadata.get("live_resume_exception"));
    check(!observed->checkpoint(), "Faulted partial-copy VM supplied a checkpoint");
    const std::array<char16_t, 3> expected{65, 90, 0};
    check(live_class->arrays_heap_usage == 10 && lsl_fixed_array_size(live_backing) == expected.size(),
          "Partial-copy class heap/backing length changed");
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(std::get<LslCharacter>(lsl_fixed_array_get(live_backing, i)).value == expected[i],
              "String2Chars pre-fault partial writes were lost");
    check(sleeps == prior_sleeps, "Derived partial-copy observation replayed Sleep");
}

void source_case(const std::filesystem::path &folder, const network::Value &metadata) {
    const auto source = read(folder / "source.lsl");
    const auto names = declarations(source);
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &entry : compiled.diagnostics)
        diagnostics += " " + entry.message;
    check(bool(compiled), "Native character source rejected:" + diagnostics);
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    auto live = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool suspended = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = live->run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted,
              "Character source faulted before Sleep: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            suspended = true;
            break;
        }
        check(result.status != LslRunStatus::Idle, "Character source completed before Sleep");
    }
    check(suspended && sleeps == 1, "Character source did not suspend exactly once");
    const auto captured = checkpoint(*live);
    compare(captured, names, metadata.get("initial"), "character source capture");
    auto internal = restore(compiled.program, host, captured, false);
    compare(checkpoint(*internal), names, metadata.get("initial"), "character internal reload");
    const bool writer_fault = metadata.get("production_write_exception").kind == network::Value::Kind::Map;
    std::unique_ptr<LslInstance> persistent;
    if (writer_fault) {
        const auto before = encode_lsl_checkpoint(captured);
        bool rejected = false;
        try {
            const auto projected = live->persistent_checkpoint();
            check(projected.has_value(), "Persistent projection missing before surrogate write");
            (void)encode_persistent_lsl_checkpoint(*projected);
        } catch (const std::exception &error) {
            fault(error.what(), metadata.get("production_write_exception"));
            rejected = true;
        }
        check(rejected && encode_lsl_checkpoint(checkpoint(*live)) == before,
              "Original surrogate Character write failure was accepted or mutated live state");
    } else {
        const auto projected = live->persistent_checkpoint();
        check(projected.has_value(), "Persistent char projection missing");
        persistent = restore(compiled.program, host, *projected, true);
        compare(checkpoint(*persistent), names, metadata.get("after_load"), "character persistent reload");
    }
    if (metadata.get("name").text == "char_partial_copy_fault")
        partial_copy_inspection(compiled.program, host, captured, names, metadata, clock, sleeps);
    resume(*live, names, metadata, false, clock, sleeps, 1);
    resume(*internal, names, metadata, false, clock, sleeps, 1);
    if (persistent)
        resume(*persistent, names, metadata, true, clock, sleeps, 1);
}

void original_import(const std::filesystem::path &folder, const network::Value &metadata) {
    const auto source = read(folder / "source.lsl");
    const auto names = declarations(source);
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Original char import source rejected");
    // Keep byte buffers alive for the decoder spans.
    const auto object_bytes = read(folder / "original.yobj");
    const auto snapshot_bytes = read(folder / "snapshot.bin");
    const auto code =
        decode_yengine_object_code({reinterpret_cast<const std::uint8_t *>(object_bytes.data()), object_bytes.size()});
    const auto saved =
        decode_yengine_snapshot({reinterpret_cast<const std::uint8_t *>(snapshot_bytes.data()), snapshot_bytes.size()});
    check(code && saved && code->source_hash == metadata.get("source_hash").text, "Original char context rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance imported(compiled.program, {}, host);
    check(imported.restore_yengine_snapshot(*saved, metadata.get("source_hash").text, source, *code),
          "Original char snapshot import rejected");
    compare(checkpoint(imported), names, metadata.get("after_load"), "actual original char import");
    check(sleeps == 0, "Original char import replayed Sleep");
    resume(imported, names, metadata, true, clock, sleeps, 0);
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original char-array fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "a74a0dc68572c1002fab963065f6494a27aa62b8770a8850c5784dd775f42062",
              "Original char proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original char proof scope changed");
        check(framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text,
              "Original char provenance changed");
        unsigned source_cases = 0, imports = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &cases = proof.get("platforms").get(platform).get("cases").array;
            check(cases.size() == 14, "Original char case inventory changed");
            for (const auto &row : cases) {
                const auto folder = root / platform / row.get("name").text;
                const auto metadata_text = read(folder / "metadata.json");
                check(framework::sha256_hex(metadata_text) == row.get("metadataSha256").text &&
                          framework::sha256_hex(read(folder / "source.lsl")) == row.get("sourceSha256").text &&
                          framework::sha256_hex(read(folder / "original.yobj")) == row.get("originalObjectSha256").text,
                      "Original char source/object/metadata changed");
                const auto metadata = network::parse_osd_json(metadata_text);
                const auto label = std::string(platform) + "/" + row.get("name").text;
                try {
                    source_case(folder, metadata);
                    ++source_cases;
                } catch (const std::exception &error) {
                    failures.push_back(label + " source: " + error.what());
                }
                if (row.get("snapshot").kind == network::Value::Kind::Map) {
                    check(framework::sha256_hex(read(folder / "snapshot.bin")) ==
                              row.get("snapshot").get("snapshotSha256").text,
                          "Original char snapshot changed");
                    try {
                        original_import(folder, metadata);
                        ++imports;
                    } catch (const std::exception &error) {
                        failures.push_back(label + " import: " + error.what());
                    }
                } else {
                    check(row.get("name").text == "char_utf16_wire_fault" &&
                              !std::filesystem::exists(folder / "snapshot.bin"),
                          "Invented original post-write-fault snapshot");
                }
            }
        }
        for (const auto &failure : failures)
            std::cerr << failure << "\n";
        check(failures.empty() && source_cases == 28 && imports == 26, "Character-array parity cases failed");
        std::cout << "PASS 28 original source cases (live/internal +26 persistent paths),26 actual snapshot imports; "
                     "partial mutation uses labeled derived observer\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}

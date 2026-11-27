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

LslCheckpoint checkpoint(const LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(result.has_value(), "Conversion evaluation checkpoint missing");
    return *result;
}

void finish(LslInstance &instance, const std::vector<std::string> &names, const network::Value &expected,
            std::int64_t &clock, unsigned &sleeps, unsigned expected_sleeps, const std::string &where) {
    clock += checkpoint(instance).sleep_remaining + 1;
    bool idle = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
              where + ": conversion replayed Sleep or faulted: " + result.error);
        if (result.status == LslRunStatus::Idle) {
            idle = true;
            break;
        }
    }
    check(idle && sleeps == expected_sleeps, where + ": execution bound or Sleep count differs");
    compare(checkpoint(instance), names, expected, where);
    check(instance.run(1).status == LslRunStatus::Idle && sleeps == expected_sleeps,
          where + ": completed event replayed");
}

void case_paths(const std::filesystem::path &folder, const network::Value &metadata) {
    const auto source = read(folder / "source.lsl");
    const auto names = declarations(source);
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &diagnostic : compiled.diagnostics)
        diagnostics += " " + diagnostic.message;
    check(bool(compiled), "Conversion evaluation source rejected:" + diagnostics);
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    auto live = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool paused = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = live->run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted,
              "Conversion source faulted before capture: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            paused = true;
            break;
        }
        check(result.status != LslRunStatus::Idle, "Conversion source completed before Sleep");
    }
    check(paused && sleeps == 1, "Conversion source did not pause exactly once");
    const auto captured = checkpoint(*live);
    compare(captured, names, metadata.get("initial"), "conversion source capture");
    auto internal = restore(compiled.program, host, captured, false);
    compare(checkpoint(*internal), names, metadata.get("initial"), "conversion internal reload");
    const auto projected = live->persistent_checkpoint();
    check(projected.has_value(), "Conversion persistent projection missing");
    auto persistent = restore(compiled.program, host, *projected, true);
    compare(checkpoint(*persistent), names, metadata.get("after_load"), "conversion persistent reload");
    // These scopes intentionally differ: source-live/internal preserve dynamic
    // String A vs literal A, while production persistence merges equal strings.
    finish(*live, names, metadata.get("live_resume"), clock, sleeps, 1, "conversion live result");
    finish(*internal, names, metadata.get("live_resume"), clock, sleeps, 1, "conversion internal result");
    finish(*persistent, names, metadata.get("final"), clock, sleeps, 1, "conversion persistent result");

    const auto object_bytes = read(folder / "original.yobj");
    const auto snapshot_bytes = read(folder / "snapshot.bin");
    const auto object =
        decode_yengine_object_code({reinterpret_cast<const std::uint8_t *>(object_bytes.data()), object_bytes.size()});
    const auto saved =
        decode_yengine_snapshot({reinterpret_cast<const std::uint8_t *>(snapshot_bytes.data()), snapshot_bytes.size()});
    check(object && saved && object->source_hash == metadata.get("source_hash").text,
          "Actual conversion object/snapshot context rejected");
    unsigned imported_sleeps = 0;
    LslHost import_host = host;
    import_host.request_sleep = [&](std::int32_t) { ++imported_sleeps; };
    LslInstance imported(compiled.program, {}, import_host);
    check(imported.restore_yengine_snapshot(*saved, metadata.get("source_hash").text, source, *object),
          "Actual original conversion continuation rejected");
    compare(checkpoint(imported), names, metadata.get("after_load"), "actual conversion import");
    check(imported_sleeps == 0, "Actual conversion import replayed Sleep");
    finish(imported, names, metadata.get("final"), clock, imported_sleeps, 0, "actual conversion resume");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original char conversion evaluation fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "f1a9a50483bafc04fe9ba7cc156dd189bd0dc454918abd8eb2a7f25e5b7cb21b",
              "Original conversion eval proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original conversion proof scope changed");
        check(framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text,
              "Original conversion provenance changed");
        unsigned checked = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &cases = proof.get("platforms").get(platform).get("cases").array;
            check(cases.size() == 5, "Conversion case inventory changed");
            for (const auto &row : cases) {
                const auto folder = root / platform / row.get("name").text;
                const auto metadata_text = read(folder / "metadata.json");
                check(framework::sha256_hex(metadata_text) == row.get("metadataSha256").text &&
                          framework::sha256_hex(read(folder / "source.lsl")) == row.get("sourceSha256").text &&
                          framework::sha256_hex(read(folder / "original.yobj")) ==
                              row.get("originalObjectSha256").text &&
                          framework::sha256_hex(read(folder / "snapshot.bin")) == row.get("snapshotSha256").text,
                      "Original conversion capture changed");
                const auto metadata = network::parse_osd_json(metadata_text);
                check(metadata.get("accepted").boolean && metadata.get("suspended").boolean &&
                          metadata.get("migrated").boolean,
                      "Original conversion source did not capture/migrate");
                if (row.get("retainedResult").kind == network::Value::Kind::Map) {
                    const auto &retained = row.get("retainedResult");
                    check(framework::sha256_hex(read(folder / "retained-strings-capture.json")) ==
                                  retained.get("captureSha256").text &&
                              framework::sha256_hex(read(folder / "retained-strings-live.json")) ==
                                  retained.get("liveSha256").text,
                          "Original retained String observation changed");
                    check(!retained.get("liveLiteralEqual").boolean && retained.get("migratedLiteralEqual").boolean &&
                              retained.get("liveReferencePreserved").boolean,
                          "Retained String live/external identity scopes changed");
                }
                try {
                    case_paths(folder, metadata);
                    ++checked;
                } catch (const std::exception &error) {
                    failures.push_back(std::string(platform) + "/" + row.get("name").text + ": " + error.what());
                }
            }
        }
        for (const auto &failure : failures)
            std::cerr << failure << "\n";
        check(failures.empty() && checked == 10, "Character conversion four-path checks failed");
        std::cout << "PASS 10 original conversion cases x4paths: live/internal/persistent/original import\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}

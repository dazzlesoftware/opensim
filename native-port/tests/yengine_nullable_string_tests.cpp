// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <iostream>
#include <memory>
#include <set>
#include <span>
using namespace utf16_test;

namespace {
std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

void compare_nullable(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto type = expected.get("type").text;
    if (type == "null") {
        // All null scalar globals in these actual sources are declared String.
        // This does not invent an object-null representation or conversion.
        check(std::holds_alternative<LslNullString>(actual), where + ": raw CLR String null identity differs");
        check(!lsl_string_code_units(actual), where + ": null String was exposed as empty text");
    } else if (type == "string" && expected.get("utf16").kind != network::Value::Kind::Array) {
        check(expected.get("clr").text == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString",
              where + ": unreviewed original nullable wrapper");
        check(std::holds_alternative<LslBoxedNullString>(actual),
              where + ": LSLString wrapper lost its null inner string");
        check(!lsl_string_code_units(actual), where + ": boxed null became empty text");
    } else if (type == "list") {
        const auto *list = std::get_if<std::shared_ptr<const LslList>>(&actual);
        const auto &items = expected.get("value").array;
        check(list && *list && (*list)->items.size() == items.size(), where + ": nullable list structure differs");
        for (std::size_t i = 0; i < items.size(); ++i)
            compare_nullable((*list)->items[i], items[i], where + "[" + std::to_string(i) + "]");
    } else
        compare_value(actual, expected, where);
}

void compare_globals_nullable(const LslCheckpoint &state, const std::string &source, const network::Value &expected) {
    const auto names = global_names(source);
    check(names.size() == state.globals.size() && names.size() == expected.map.size(), "Nullable global count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        compare_nullable(state.globals[i], expected.get(names[i]), names[i]);
}

void compare_null_identity(const LslValue &before, const LslValue &after) {
    check(std::holds_alternative<LslNullString>(before) == std::holds_alternative<LslNullString>(after) &&
              std::holds_alternative<LslBoxedNullString>(before) == std::holds_alternative<LslBoxedNullString>(after) &&
              std::holds_alternative<LslBoxedNullKey>(before) == std::holds_alternative<LslBoxedNullKey>(after),
          "Internal checkpoint changed nullable carrier identity");
    compare_checkpoint_strings(before, after);
    if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(&before)) {
        const auto *restored = std::get_if<std::shared_ptr<const LslList>>(&after);
        check(list && *list && restored && *restored && (*list)->items.size() == (*restored)->items.size(),
              "Nullable checkpoint list differs");
        for (std::size_t i = 0; i < (*list)->items.size(); ++i)
            compare_null_identity((*list)->items[i], (*restored)->items[i]);
    }
}

std::string provenance(const std::filesystem::path &root, const network::Value &oracle) {
    const auto folder = root / oracle.get("name").text;
    const auto source = read(folder / "source.lsl");
    if (oracle.get("accepted").boolean) {
        check(framework::sha256_hex(source) == oracle.get("sha256").get("source.lsl").text &&
                  yengine_source_matches(source, oracle.get("source_hash").text),
              "Nullable original source identity differs");
        for (const auto *name : {"original.yobj", "original.il.txt"})
            check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
                  "Nullable original compiler artifact differs");
    }
    return source;
}

void source_fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto source = provenance(root, oracle);
    const auto compiled = compile_lsl(source);
    if (!oracle.get("accepted").boolean) {
        check(!compiled, "Original rejected String-to-Char cast unexpectedly accepted");
        return;
    }
    std::string diagnostics;
    for (const auto &error : compiled.diagnostics)
        diagnostics += " " + error.message;
    check(bool(compiled), "Accepted original nullable source failed compile:" + diagnostics);
    const auto &exception = oracle.get("initial_exception");
    const bool fault = exception.kind == network::Value::Kind::Map;
    if (fault)
        check(exception.get("type").text == "System.NullReferenceException", "Unreviewed original nullable fault");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    auto last = instance->checkpoint();
    check(last.has_value(), "Initial nullable checkpoint unavailable");
    bool slept = false, saw16 = false;
    for (unsigned step = 0; step < 3000; ++step) {
        const auto result = instance->run(1);
        if (result.status == LslRunStatus::Faulted) {
            check(fault && result.error.starts_with("NullReferenceException:"),
                  "Nullable runtime fault category differs: " + result.error);
            check(saw16, "Nullable pre-fault state did not survive checkpoint16");
            compare_globals_nullable(*last, source, oracle.get("final").get("globals"));
            return;
        }
        last = instance->checkpoint();
        check(last.has_value(), "Nullable source checkpoint unavailable");
        const auto wire = encode_lsl_checkpoint(*last);
        saw16 |= wire.size() > 3 && wire[3] == 16;
        const auto decoded = decode_lsl_checkpoint(wire);
        check(decoded.has_value(), "Nullable checkpoint16 codec rejected state");
        check(decoded->globals.size() == last->globals.size(), "Nullable checkpoint global count changed");
        for (std::size_t i = 0; i < last->globals.size(); ++i)
            compare_null_identity(last->globals[i], decoded->globals[i]);
        auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected nullable continuation");
        instance = std::move(fresh);
        if (result.status == LslRunStatus::Sleeping) {
            check(oracle.get("suspended").boolean && !slept, "Nullable source sleep unexpectedly replayed");
            slept = true;
            compare_globals_nullable(*last, source, oracle.get("initial").get("globals"));
            clock += last->sleep_remaining + 1;
        }
        if (result.status == LslRunStatus::Idle) {
            check(!fault, "Original nullable NullReferenceException was replaced by completion");
            check(saw16, "Nullable source never exported checkpoint16");
            check(slept == oracle.get("suspended").boolean && sleeps == (slept ? 1u : 0u),
                  "Nullable sleep count differs");
            compare_globals_nullable(*last, source, oracle.get("final").get("globals"));
            check(instance->run(1).status == LslRunStatus::Idle, "Completed nullable source replayed entry");
            compare_globals_nullable(*instance->checkpoint(), source, oracle.get("final").get("globals"));
            return;
        }
    }
    throw std::runtime_error("Nullable source exceeded bounded execution");
}

void original_import(const std::filesystem::path &root, const network::Value &oracle) {
    check(oracle.get("name").text == "production_migration" && oracle.get("suspended").boolean &&
              oracle.get("migrated").boolean &&
              oracle.get("production_write_exception").kind != network::Value::Kind::Map &&
              oracle.get("resume_exception").kind != network::Value::Kind::Map,
          "Original nullable production migration did not succeed");
    const auto source = provenance(root, oracle);
    const auto folder = root / "production_migration";
    const auto wire = read(folder / "snapshot.bin");
    check(framework::sha256_hex(wire) == oracle.get("sha256").get("snapshot.bin").text,
          "Original nullable snapshot hash differs");
    const auto code_wire = read(folder / "original.yobj");
    const auto code = decode_yengine_object_code(bytes(code_wire));
    const auto snapshot = decode_yengine_snapshot(bytes(wire));
    check(code && snapshot, "Original nullable bytes did not decode");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Original nullable import source rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    check(instance->restore_yengine_snapshot(*snapshot, oracle.get("source_hash").text, source, *code),
          "Genuine original raw/boxed nullable snapshot import rejected");
    compare_globals_nullable(*instance->checkpoint(), source, oracle.get("after_load").get("globals"));
    for (unsigned step = 0; step < 3000; ++step) {
        const auto result = instance->run(1);
        check(result.status != LslRunStatus::Faulted, "Imported nullable continuation faulted: " + result.error);
        check(result.status != LslRunStatus::Sleeping, "Imported nullable continuation replayed original llSleep");
        const auto current = instance->checkpoint();
        check(current.has_value(), "Imported nullable checkpoint unavailable");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*current));
        check(decoded.has_value(), "Imported nullable codec failed");
        auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected imported nullable state");
        instance = std::move(fresh);
        if (result.status == LslRunStatus::Idle) {
            check(sleeps == 0, "Imported nullable API prefix replayed");
            compare_globals_nullable(*current, source, oracle.get("final").get("globals"));
            check(instance->run(1).status == LslRunStatus::Idle, "Imported completed nullable event replayed");
            return;
        }
    }
    throw std::runtime_error("Imported nullable continuation exceeded bounded execution");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Original nullable20 and crypto134 fixture directories required");
        const std::filesystem::path root = argv[1], crypto_root = argv[2];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 20, "Original nullable source inventory changed");
        const std::set<std::string> crypto_names = {"compute_high_7", "compute_low_7",     "compute_pair_7",
                                                    "compute_nul_7",  "hmac_widekey_6",    "hmac_widemessage_6",
                                                    "hmac_widekey_7", "hmac_widemessage_7"};
        std::set<std::string> seen;
        std::vector<std::string> failures;
        unsigned accepted = 0, faults = 0;
        for (const auto &oracle : cases.array) {
            accepted += oracle.get("accepted").boolean;
            faults += oracle.get("initial_exception").kind == network::Value::Kind::Map;
            try {
                source_fixture(root, oracle);
                if (oracle.get("name").text == "production_migration")
                    original_import(root, oracle);
            } catch (const std::exception &error) {
                failures.push_back(oracle.get("name").text + ": " + error.what());
            }
        }
        check(accepted == 19 && faults == 3, "Original nullable acceptance/fault inventory changed");
        const auto crypto = network::parse_osd_json(read(crypto_root / "metadata.json"));
        check(crypto.array.size() == 134, "Historical crypto fixture changed");
        for (const auto &oracle : crypto.array)
            if (crypto_names.contains(oracle.get("name").text)) {
                check(seen.insert(oracle.get("name").text).second, "Duplicate original nullable crypto source");
                check(oracle.get("final").get("globals").get("result").get("type").text == "null",
                      "Original crypto row no longer proves null result");
                try {
                    source_fixture(crypto_root, oracle);
                } catch (const std::exception &error) {
                    failures.push_back(oracle.get("name").text + ": " + error.what());
                }
            }
        check(seen == crypto_names, "Missing original nullable crypto control");
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        check(failures.empty(), "Nullable source/import checks failed");
        std::cout << "28 actual nullable sources checked:24 complete,3 NullReference faults,1 rejected; genuine "
                     "raw/boxed-null import resumes without replay.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

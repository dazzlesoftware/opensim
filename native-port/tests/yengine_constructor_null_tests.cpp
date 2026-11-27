// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <iostream>
#include <memory>
#include <regex>
using namespace utf16_test;

namespace {
using Declaration = std::pair<std::string, std::string>;

std::vector<Declaration> declarations(std::string source) {
    source = global_declaration_source(std::move(source));
    const std::regex pattern(
        R"(\b(char|integer|float|string|key|list|vector|rotation)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<Declaration> result;
    for (auto i = std::sregex_iterator(source.begin(), source.end(), pattern); i != std::sregex_iterator(); ++i)
        result.emplace_back((*i)[1].str(), (*i)[2].str());
    return result;
}

void value(const LslValue &actual, const network::Value &expected, const std::string &type, const std::string &where) {
    if (expected.get("type").text == "null") {
        if (type == "list")
            check(std::holds_alternative<LslNullList>(actual), where + ": typed List null lost");
        else if (type == "string" || type == "key")
            check(std::holds_alternative<LslNullString>(actual), where + ": typed String/Key null lost");
        else
            check(std::holds_alternative<LslNullObject>(actual), where + ": raw Object null lost");
    } else if (expected.get("type").text == "list") {
        const auto *list = std::get_if<std::shared_ptr<const LslList>>(&actual);
        check(list && *list && (*list)->items.size() == expected.get("value").array.size(),
              where + ": list structure differs");
        for (std::size_t i = 0; i < (*list)->items.size(); ++i)
            value((*list)->items[i], expected.get("value").array[i], "object", where + "[" + std::to_string(i) + "]");
    } else
        compare_value(actual, expected, where, type == "string" || type == "key");
}

void globals(const LslCheckpoint &saved, const std::string &source, const network::Value &expected) {
    const auto names = declarations(source);
    check(names.size() == saved.globals.size() && names.size() == expected.map.size(),
          "Constructor global count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        value(saved.globals[i], expected.get(names[i].second), names[i].first, names[i].second);
}

std::string provenance(const std::filesystem::path &root, const network::Value &oracle) {
    const auto folder = root / oracle.get("name").text;
    const auto source = read(folder / "source.lsl");
    check(oracle.get("accepted").boolean && yengine_source_matches(source, oracle.get("source_hash").text),
          "Original constructor source identity differs");
    for (const auto *name : {"source.lsl", "original.yobj", "original.il.txt"})
        check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
              "Original constructor artifact differs");
    return source;
}

std::span<const std::uint8_t> bytes(const std::string &s) {
    return {reinterpret_cast<const std::uint8_t *>(s.data()), s.size()};
}

void run_fixture(const std::filesystem::path &root, const network::Value &oracle, bool import) {
    const auto source = provenance(root, oracle);
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &d : compiled.diagnostics)
        diagnostics += " " + d.message;
    check(bool(compiled), "Accepted original constructor source failed compile:" + diagnostics);
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int ms) {
        check(ms == 3000, "Constructor sleep duration differs");
        ++sleeps;
    };
    auto vm = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    const auto &error = oracle.get("initial_exception");
    const bool fault = error.kind == network::Value::Kind::Map;
    if (import) {
        check(oracle.get("suspended").boolean && oracle.get("migrated").boolean &&
                  oracle.get("production_write_exception").kind != network::Value::Kind::Map,
              "Original constructor production migration failed");
        const auto folder = root / oracle.get("name").text;
        const auto raw = read(folder / "snapshot.bin"), object = read(folder / "original.yobj");
        check(framework::sha256_hex(raw) == oracle.get("sha256").get("snapshot.bin").text,
              "Original constructor migration hash differs");
        const auto snapshot = decode_yengine_snapshot(bytes(raw));
        const auto code = decode_yengine_object_code(bytes(object));
        check(snapshot && code &&
                  vm->restore_yengine_snapshot(*snapshot, oracle.get("source_hash").text, source, *code),
              "Original constructor snapshot import rejected");
        globals(*vm->checkpoint(), source, oracle.get("after_load").get("globals"));
    }
    auto last = vm->checkpoint();
    check(bool(last), "Initial constructor checkpoint missing");
    bool saw_sleep = false;
    for (unsigned step = 0; step < 4000; ++step) {
        const auto result = vm->run(1);
        if (result.status == LslRunStatus::Faulted) {
            check(fault && !import, "Successful original constructor faulted: " + result.error);
            auto type = error.get("type").text;
            check(type == "System.NullReferenceException" || type == "System.Exception",
                  "Unreviewed constructor exception");
            const auto prefix = type == "System.Exception" ? "Exception" : type.substr(7);
            check(result.error == prefix + ": " + error.get("message").text,
                  "Constructor fault category/message differs: " + result.error);
            globals(*last, source, oracle.get("final").get("globals"));
            return;
        }
        last = vm->checkpoint();
        check(bool(last), "Constructor checkpoint missing");
        const auto wire = encode_lsl_checkpoint(*last);
        const auto decoded = decode_lsl_checkpoint(wire);
        check(decoded && encode_lsl_checkpoint(*decoded) == wire,
              "Constructor checkpoint identity/aliases/units changed");
        auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected constructor continuation");
        vm = std::move(fresh);
        if (result.status == LslRunStatus::Sleeping) {
            check(!import && !saw_sleep && oracle.get("suspended").boolean,
                  "Constructor sleep replayed or appeared after original import");
            globals(*last, source, oracle.get("initial").get("globals"));
            saw_sleep = true;
            clock += last->sleep_remaining + 1;
        } else if (result.status == LslRunStatus::Idle) {
            check(!fault, "Original constructor fault replaced by success");
            const auto &expected =
                import || !oracle.get("suspended").boolean ? oracle.get("final") : oracle.get("live_resume");
            globals(*last, source, expected.get("globals"));
            check(sleeps == (import ? 0u : (oracle.get("suspended").boolean ? 1u : 0u)),
                  "Constructor API prefix replayed");
            check(vm->run(1).status == LslRunStatus::Idle, "Completed constructor event replayed");
            globals(*vm->checkpoint(), source, expected.get("globals"));
            return;
        } else
            check(result.status == LslRunStatus::BudgetExhausted, "Unexpected constructor execution status");
    }
    throw std::runtime_error("Constructor execution exceeded bound");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Original constructor18 and supplement7 required");
        unsigned faults = 0, captures = 0;
        for (int arg = 1; arg <= 2; ++arg) {
            const std::filesystem::path root = argv[arg];
            const auto rows = network::parse_osd_json(read(root / "metadata.json"));
            check(rows.array.size() == (arg == 1 ? 18 : 7), "Original constructor inventory changed");
            for (const auto &row : rows.array) {
                faults += row.get("initial_exception").kind == network::Value::Kind::Map;
                captures += row.get("suspended").boolean;
                try {
                    run_fixture(root, row, false);
                    if (row.get("suspended").boolean)
                        run_fixture(root, row, true);
                } catch (const std::exception &e) {
                    throw std::runtime_error(row.get("name").text + ": " + e.what());
                }
            }
        }
        check(faults == 7 && captures == 5, "Original constructor outcome counts differ");
        std::cout << "PASS: constructor25,18 successes/7 exact faults,5 original production imports,budget1 live "
                     "checkpoints\nPENDING: native external zero-Rotation normalization; original import and native "
                     "lossless live paths tested separately\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

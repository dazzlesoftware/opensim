// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include <bit>
#include <iostream>
#include <memory>
#include <sstream>
using namespace utf16_test;

namespace {
void fixture(const std::filesystem::path &root, const network::Value &row) {
    const auto name = row.get("name").text;
    auto folder = root / name;
    auto source = read(folder / "source.lsl");
    const auto compiled = compile_lsl(source);
    check(bool(compiled) == row.get("accepted").boolean, name + ": compile acceptance differs from original");
    if (!row.get("accepted").boolean) {
        check(!row.get("diagnostics").array.empty() || row.get("compile_exception").kind == network::Value::Kind::Map,
              name + ": original rejection has no diagnostic witness");
        return;
    }
    for (const auto *file : {"source.lsl", "original.yobj", "original.il.txt"})
        check(framework::sha256_hex(read(folder / file)) == row.get("sha256").get(file).text,
              name + ": original artifact changed");
    const bool hash_matches = yengine_source_matches(source, row.get("source_hash").text);
    bool token = false;
    for (const auto &item : row.get("tokens").array)
        if (item.get("type").text == "TokenFloat") {
            check(item.get("float_bits").text.size() == 16, name + ": original float token bits missing");
            token = true;
        }
    check(token, name + ": accepted source lacks genuine original Float token");
    const auto &expected = row.get("final").get("globals");
    const auto expected_bits = std::stoull(expected.get("value").get("bits").text, nullptr, 16);
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{638000000000000000LL}; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    for (unsigned step = 0; step < 1000; ++step) {
        auto outcome = instance->run(1);
        check(outcome.status != LslRunStatus::Faulted, name + ": execution fault: " + outcome.error);
        auto checkpoint = instance->checkpoint();
        check(checkpoint.has_value(), name + ": missing checkpoint");
        auto wire = encode_lsl_checkpoint(*checkpoint);
        auto saved = decode_lsl_checkpoint(wire);
        check(saved.has_value(), name + ": checkpoint decode failed");
        auto next = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(next->restore_checkpoint(*saved), name + ": fresh VM restore failed");
        instance = std::move(next);
        if (outcome.status == LslRunStatus::Idle) {
            check(saved->globals.size() == 2, name + ": global count differs");
            check(std::holds_alternative<double>(saved->globals.at(0)), name + ": Float storage type differs");
            const auto actual_bits = std::bit_cast<std::uint64_t>(std::get<double>(saved->globals.at(0)));
            std::ostringstream diagnostics;
            if (!hash_matches)
                diagnostics << name << ": original token hash differs; ";
            if (actual_bits != expected_bits)
                diagnostics << name << ": literal IEEE bits actual=" << std::hex << actual_bits
                            << " expected=" << expected_bits << "; ";
            check(hash_matches && actual_bits == expected_bits, diagnostics.str());
            check(std::get<std::int32_t>(saved->globals.at(1)) == 1, name + ": original entry did not complete");
            check(instance->run(1).status == LslRunStatus::Idle, name + ": entry replayed after restore");
            return;
        }
    }
    throw std::runtime_error(name + ": execution exceeded bound");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original Float literal fixture directory");
        auto root = std::filesystem::path(argv[1]);
        auto text = read(root / "metadata.json");
        check(framework::sha256_hex(text) == "8d5cc612748a90055fc1bd0ffc3564c6e1f3b135b504d20894e9f6ad26249294",
              "Original float metadata changed");
        auto rows = network::parse_osd_json(text);
        check(rows.array.size() == 76, "Original Float case count differs");
        unsigned accepted = 0, rejected = 0;
        std::string failures;
        for (const auto &row : rows.array) {
            row.get("accepted").boolean ? ++accepted : ++rejected;
            try {
                fixture(root, row);
            } catch (const std::exception &e) {
                failures += std::string(e.what()) + '\n';
            }
        }
        check(accepted == 42 && rejected == 34, "Original Float outcome inventory differs");
        check(failures.empty(), failures);
        std::cout << "76 original Float literal contracts passed:42 accepted,34 rejected\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

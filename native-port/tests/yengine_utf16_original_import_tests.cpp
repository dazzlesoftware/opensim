// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <iostream>
#include <memory>
#include <span>
using namespace utf16_test;

namespace {
std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto folder = root / oracle.get("name").text, source_path = folder / "source.lsl";
    const auto source = read(source_path);
    check(oracle.get("accepted").boolean && oracle.get("suspended").boolean && oracle.get("migrated").boolean &&
              oracle.get("production_write_exception").kind != network::Value::Kind::Map &&
              oracle.get("resume_exception").kind != network::Value::Kind::Map,
          "Actual original production capture did not succeed");
    for (const auto *name : {"source.lsl", "original.yobj", "snapshot.bin", "original.il.txt"})
        check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
              "Original production import artifact hash differs");
    check(yengine_source_matches(source, oracle.get("source_hash").text),
          "Original production source identity differs");
    const auto code_bytes = read(folder / "original.yobj"), snapshot_bytes = read(folder / "snapshot.bin");
    const auto code = decode_yengine_object_code(bytes(code_bytes));
    const auto snapshot = decode_yengine_snapshot(bytes(snapshot_bytes));
    check(code && snapshot, "Actual original production bytes did not decode");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Actual original production source failed native compile");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    context.owner = framework::Uuid::random();
    auto instance = std::make_unique<LslInstance>(compiled.program, context, host);
    check(instance->restore_yengine_snapshot(*snapshot, oracle.get("source_hash").text, source, *code),
          "Genuine original production UTF16 snapshot import rejected");
    const auto start = instance->checkpoint();
    check(start.has_value(), "Imported production state checkpoint unavailable");
    compare_globals(*start, global_names(source), oracle.get("after_load").get("globals"));
    for (unsigned step = 0; step < 3000; ++step) {
        const auto result = instance->run(1);
        check(result.status != LslRunStatus::Faulted, "Original production continuation faulted: " + result.error);
        check(result.status != LslRunStatus::Sleeping, "Original completed llSleep replayed");
        const auto current = instance->checkpoint();
        check(current.has_value(), "Original production continuation checkpoint unavailable");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*current));
        check(decoded.has_value(), "Imported continuation native codec failed");
        auto fresh = std::make_unique<LslInstance>(compiled.program, context, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected imported original state");
        instance = std::move(fresh);
        if (result.status == LslRunStatus::Idle) {
            compare_globals(*current, global_names(source), oracle.get("final").get("globals"));
            check(sleeps == 0, "Original source API prefix replayed");
            check(instance->run(1).status == LslRunStatus::Idle, "Imported completed entry replayed");
            compare_globals(*instance->checkpoint(), global_names(source), oracle.get("final").get("globals"));
            return;
        }
    }
    throw std::runtime_error("Original production continuation exceeded bounded execution");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Genuine original production String3 fixture directory required");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 3, "Original production String3 inventory changed");
        std::vector<std::string> failures;
        for (const auto &oracle : cases.array)
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                failures.push_back(oracle.get("name").text + ": " + error.what());
            }
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        check(failures.empty(), "Original production String3 import cases failed");
        std::cout
            << "Three genuine production String snapshots imported/resumed exact original values without replay.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

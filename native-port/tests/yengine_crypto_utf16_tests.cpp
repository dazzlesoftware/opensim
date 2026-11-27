// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include <iostream>
using namespace utf16_test;

namespace {
void provenance(const std::filesystem::path &root, const network::Value &oracle) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    check(oracle.get("accepted").boolean && yengine_source_matches(source, oracle.get("source_hash").text),
          "Original crypto source/provenance differs");
    for (const auto *artifact : {"source.lsl", "original.yobj", "original.il.txt"})
        check(framework::sha256_hex(read(root / name / artifact)) == oracle.get("sha256").get(artifact).text,
              std::string("Original crypto artifact SHA256 differs: ") + artifact);
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    provenance(root, oracle);
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    const auto program = compile_lsl(source);
    std::string errors;
    for (const auto &error : program.diagnostics)
        errors += " " + error.message;
    check(bool(program), "Accepted original crypto source failed compile:" + errors);
    std::int64_t clock = 638000000000000000LL;
    std::vector<int> delays;
    std::vector<LslChat> chats, listeners;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int delay) { delays.push_back(delay); };
    host.chat = [&](const LslChat &value) { chats.push_back(value); };
    host.listener = [&](const LslChat &value) { listeners.push_back(value); };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    context.owner = framework::Uuid::random();
    context.group = framework::Uuid::random();
    LslInstance instance(program.program, context, host);
    auto saved = instance.checkpoint();
    check(bool(saved), "Initial crypto checkpoint missing");
    LslRunResult outcome;
    bool complete = false;
    for (unsigned step = 0; step < 2000; ++step) {
        outcome = instance.run(1);
        if (const auto value = instance.checkpoint())
            saved = value;
        if (outcome.status == LslRunStatus::Faulted || outcome.status == LslRunStatus::Idle) {
            complete = true;
            break;
        }
        check(outcome.status == LslRunStatus::Sleeping || outcome.status == LslRunStatus::BudgetExhausted,
              "Unexpected crypto execution status");
        const auto wire = encode_lsl_checkpoint(*saved);
        const auto decoded = decode_lsl_checkpoint(wire);
        LslInstance fresh(program.program, context, host);
        check(decoded && fresh.restore_checkpoint(*decoded), "Lossless crypto checkpoint fresh-VM restoration failed");
        const auto restored = fresh.checkpoint();
        check(bool(restored), "Fresh crypto checkpoint missing");
        check(restored->globals.size() == saved->globals.size(), "Fresh crypto globals changed");
        for (std::size_t i = 0; i < saved->globals.size(); ++i)
            compare_checkpoint_strings(saved->globals[i], restored->globals[i]);
        instance = std::move(fresh);
        if (outcome.status == LslRunStatus::Sleeping)
            clock += 20000000;
    }
    check(complete, "Crypto execution exceeded source bound");
    const auto &error = oracle.get("initial_exception");
    if (error.kind == network::Value::Kind::Map) {
        check(error.get("type").text == "System.DivideByZeroException", "Unreviewed actual original crypto exception");
        check(outcome.status == LslRunStatus::Faulted && outcome.error.starts_with("DivideByZeroException: "),
              "Original legacy XOR exception category differs: " + outcome.error);
    } else
        check(outcome.status == LslRunStatus::Idle, "Successful original crypto source faulted: " + outcome.error);
    const auto &expected = oracle.get("suspended").boolean ? oracle.get("live_resume") : oracle.get("final");
    compare_globals(*saved, global_names(source), expected.get("globals"), declared_string_globals(source));
    const auto &original_chats = oracle.get("observed_scene_chats").array;
    check(chats.size() == original_chats.size(), "Original crypto error chat count differs");
    for (std::size_t i = 0; i < chats.size(); ++i) {
        const auto &expected_chat = original_chats[i];
        check(expected_chat.get("chat_type").text == "DebugChannel" && chats[i].type == LslChatType::Debug &&
                  chats[i].channel == network::osd_integer(expected_chat.get("channel")) &&
                  chats[i].text == expected_chat.get("text").text && chats[i].use_root_position,
              "Original Base64 error chat audience/text/root position differs");
    }
    if (!original_chats.empty()) {
        check(delays == std::vector<int>{1000} && listeners.size() == 1 && listeners[0].type == LslChatType::Shout &&
                  listeners[0].text == chats[0].text,
              "Base64 error delay/listener replay or channel semantics differ");
    } else if (name.starts_with("llXorBase64Strings_") || name.starts_with("legacy_"))
        check(delays == std::vector<int>{300}, "Deprecated XOR sleep replayed or disappeared");
    else
        check(delays.empty() && listeners.empty(), "Pure crypto source introduced unrequested effects");
    if (error.kind != network::Value::Kind::Map) {
        const auto wire = encode_lsl_checkpoint(*saved);
        const auto decoded = decode_lsl_checkpoint(wire);
        LslInstance final(program.program, context, host);
        check(decoded && final.restore_checkpoint(*decoded), "Final crypto checkpoint fresh-VM restore failed");
        const auto restored = final.checkpoint();
        check(bool(restored), "Final crypto restored state missing");
        compare_globals(*restored, global_names(source), expected.get("globals"), declared_string_globals(source));
        const auto chat_count = chats.size(), listener_count = listeners.size(), delay_count = delays.size();
        check(final.run(1).status == LslRunStatus::Idle && chats.size() == chat_count &&
                  listeners.size() == listener_count && delays.size() == delay_count,
              "Completed crypto checkpoint replayed script/API effects after restore");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original crypto UTF16 fixture directory");
        const std::filesystem::path root(argv[1]);
        const auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 134, "Original crypto corpus count differs");
        std::size_t faults = 0, captured = 0, errors = 0, verified = 0;
        for (const auto &row : rows.array) {
            faults += row.get("initial_exception").kind == network::Value::Kind::Map;
            captured += row.get("suspended").boolean;
            errors += !row.get("observed_scene_chats").array.empty();
            try {
                fixture(root, row);
                ++verified;
            } catch (const std::exception &e) {
                throw std::runtime_error(row.get("name").text + ": " + e.what());
            }
        }
        check(verified == 134, "Crypto native verification accounting differs");
        check(faults == 3 && captured == 7 && errors == 7, "Original crypto effect/outcome accounting differs");
        std::cout << "PASS: 134 native-verified UTF16 crypto/base64 cases including eight nullable-string results; "
                     "three legacy faults, seven captured sleeps and seven decoder error cases, exact units/hash "
                     "bytes and budget1 fresh-VM checkpoints\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

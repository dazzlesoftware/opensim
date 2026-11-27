// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include <array>
#include <iostream>
#include <memory>
using namespace utf16_test;

namespace {
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

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    check(oracle.get("accepted").boolean, "Original nullable API source unexpectedly rejected");
    const auto source = provenance(root, oracle);
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &d : compiled.diagnostics)
        diagnostics += " " + d.message;
    check(bool(compiled), "Accepted original SDK source failed compile:" + diagnostics);
    const auto &exception = oracle.get("initial_exception");
    const bool fault = exception.kind == network::Value::Kind::Map;
    std::int64_t clock = 638000000000000000LL;
    std::vector<int> delays;
    std::vector<LslChat> chats, listeners;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int value) { delays.push_back(value); };
    host.chat = [&](const LslChat &v) { chats.push_back(v); };
    host.listener = [&](const LslChat &v) { listeners.push_back(v); };
    LslContext context;
    context.object = framework::Uuid::random();
    context.owner = framework::Uuid::random();
    context.item = framework::Uuid::random();
    auto vm = std::make_unique<LslInstance>(compiled.program, context, host);
    auto last = vm->checkpoint();
    check(bool(last), "Initial nullable API state missing");
    bool complete = false;
    for (unsigned step = 0; step < 3000; ++step) {
        const auto result = vm->run(1);
        if (result.status == LslRunStatus::Faulted) {
            check(fault, "Successful original SDK source faulted: " + result.error);
            auto type = exception.get("type").text;
            check(type == "System.NullReferenceException" || type == "System.ArgumentNullException",
                  "Unreviewed nullable API fault");
            check(result.error == type.substr(7) + ": " + exception.get("message").text,
                  "Nullable API exception text/category differs: " + result.error);
            compare_globals_nullable(*last, source, oracle.get("final").get("globals"));
            complete = true;
            break;
        }
        last = vm->checkpoint();
        check(bool(last), "Nullable API checkpoint missing");
        const auto wire = encode_lsl_checkpoint(*last);
        const auto d = decode_lsl_checkpoint(wire);
        check(bool(d), "Nullable API lossless wire failed decode");
        check(d->globals.size() == last->globals.size(), "Nullable API global count changed");
        for (std::size_t i = 0; i < last->globals.size(); ++i)
            compare_null_identity(last->globals[i], d->globals[i]);
        auto fresh = std::make_unique<LslInstance>(compiled.program, context, host);
        check(fresh->restore_checkpoint(*d), "Fresh VM rejected nullable SDK continuation");
        vm = std::move(fresh);
        if (result.status == LslRunStatus::Sleeping)
            clock += last->sleep_remaining + 1;
        else if (result.status == LslRunStatus::Idle) {
            check(!fault, "Original nullable SDK fault replaced by success");
            compare_globals_nullable(*last, source, oracle.get("final").get("globals"));
            const auto effects = std::array{delays.size(), chats.size(), listeners.size()};
            check(vm->run(1).status == LslRunStatus::Idle, "Completed nullable SDK entry replayed");
            check(effects == std::array{delays.size(), chats.size(), listeners.size()},
                  "Completed nullable SDK effects replayed");
            compare_globals_nullable(*vm->checkpoint(), source, oracle.get("final").get("globals"));
            complete = true;
            break;
        } else
            check(result.status == LslRunStatus::BudgetExhausted, "Unexpected nullable SDK status");
    }
    check(complete, "Nullable API source exceeded execution bound");
    const auto &expected = oracle.get("observed_scene_chats").array;
    check(chats.size() == expected.size(), "Nullable SDK error chat count differs");
    for (std::size_t i = 0; i < chats.size(); ++i) {
        check(expected[i].get("chat_type").text == "DebugChannel" && chats[i].type == LslChatType::Debug &&
                  chats[i].channel == network::osd_integer(expected[i].get("channel")) &&
                  chats[i].text == expected[i].get("text").text && chats[i].use_root_position,
              "Nullable SDK error chat text/audience/position differs");
    }
    if (expected.empty())
        check(delays.empty() && listeners.empty(), "Pure nullable API introduced effects");
    else
        check(expected.size() == 1 && delays == std::vector<int>{1000} && listeners.size() == 1 &&
                  listeners[0].type == LslChatType::Shout && listeners[0].channel == chats[0].channel &&
                  listeners[0].text == chats[0].text,
              "Nullable SDK error sleep/listener replay or boundary differs");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Original nullable API40 and HMAC guard6 fixture directories required");
        const std::filesystem::path root = argv[1];
        const auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 40, "Original nullable API inventory changed");
        unsigned faults = 0, chats = 0;
        for (const auto &row : rows.array) {
            faults += row.get("initial_exception").kind == network::Value::Kind::Map;
            chats += !row.get("observed_scene_chats").array.empty();
            try {
                fixture(root, row);
            } catch (const std::exception &e) {
                throw std::runtime_error(row.get("name").text + ": " + e.what());
            }
        }
        check(faults == 12 && chats == 2, "Original nullable API outcome counts differ");
        const std::filesystem::path guard_root = argv[2];
        const auto guards = network::parse_osd_json(read(guard_root / "metadata.json"));
        check(guards.array.size() == 6, "Original HMAC guard inventory changed");
        unsigned guard_faults = 0;
        for (const auto &row : guards.array) {
            guard_faults += row.get("initial_exception").kind == network::Value::Kind::Map;
            check(row.get("observed_scene_chats").array.empty(), "Original HMAC guard unexpectedly emitted chat");
            try {
                fixture(guard_root, row);
            } catch (const std::exception &e) {
                throw std::runtime_error(row.get("name").text + ": " + e.what());
            }
        }
        check(guard_faults == 3, "Original HMAC guard fault/success counts differ");
        std::cout << "PASS: 40 original nullable SDK cases,28 successes,12 exact faults,2 error chats/delays and "
                     "budget1 fresh-VM checks;6 HMAC guards (3 successes/3 faults)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

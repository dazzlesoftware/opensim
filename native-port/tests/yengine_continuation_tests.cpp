// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace script;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Could not read original continuation fixture: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

auto bytes(const std::string &value) {
    return std::span(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
}

void finish(LslInstance &instance) {
    for (int step = 0; step < 10000; ++step) {
        const auto result = instance.run(3);
        check(result.status != LslRunStatus::Faulted, "Translated continuation faulted: " + result.error);
        check(result.status != LslRunStatus::Sleeping, "Loop-suspended original fixture unexpectedly slept");
        if (result.status == LslRunStatus::Idle)
            return;
    }
    throw std::runtime_error("Translated continuation exceeded its instruction bound");
}

std::string list_text(const LslValue &boxed) {
    const auto value = unbox_lsl_value(boxed);
    if (const auto integer = std::get_if<std::int32_t>(&value))
        return std::to_string(*integer);
    if (const auto text = std::get_if<std::string>(&value))
        return *text;
    if (const auto list = std::get_if<std::shared_ptr<const LslList>>(&value)) {
        std::string text;
        if (*list)
            for (const auto &item : (*list)->items)
                text += list_text(item);
        return text;
    }
    throw std::runtime_error("Unexpected original list-oracle scalar type");
}

std::vector<std::string> global_names(const std::string &source) {
    // These original fixture scripts have global declarations and no local
    // declarations. Read their declaration order without embedding fixture slot
    // numbers or compiler-generated function names in the assertions.
    const std::regex declaration(
        R"(\b(?:integer|float|string|key|vector|rotation|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> names;
    for (auto match = std::sregex_iterator(source.begin(), source.end(), declaration); match != std::sregex_iterator();
         ++match)
        names.push_back((*match)[1].str());
    return names;
}

void compare_typed_value(const LslValue &boxed, const network::Value &expected, const std::string &name) {
    const auto actual = unbox_lsl_value(boxed);
    const auto type = expected.get("type").text;
    const auto &value = expected.get("value");
    const auto failure = "Resumed native typed value differs from unchanged original execution: " + name;
    if (type == "integer") {
        const auto item = std::get_if<std::int32_t>(&actual);
        check(item && *item == network::osd_integer(value), failure);
    } else if (type == "float") {
        const auto item = std::get_if<double>(&actual);
        check(item && *item == network::osd_real(value), failure);
    } else if (type == "string" || type == "key") {
        const auto item = std::get_if<std::string>(&actual);
        check(item && value.kind == network::Value::Kind::String && *item == value.text, failure);
    } else if (type == "vector") {
        const auto item = std::get_if<Vector3d>(&actual);
        check(item && value.array.size() == 3 && item->x == network::osd_real(value.array[0]) &&
                  item->y == network::osd_real(value.array[1]) && item->z == network::osd_real(value.array[2]),
              failure);
    } else if (type == "rotation") {
        const auto item = std::get_if<LslRotation>(&actual);
        check(item && value.array.size() == 4 && item->x == network::osd_real(value.array[0]) &&
                  item->y == network::osd_real(value.array[1]) && item->z == network::osd_real(value.array[2]) &&
                  item->w == network::osd_real(value.array[3]),
              failure);
    } else if (type == "list") {
        const auto item = std::get_if<std::shared_ptr<const LslList>>(&actual);
        check(item && *item && value.kind == network::Value::Kind::Array && (*item)->items.size() == value.array.size(),
              failure);
        for (std::size_t index = 0; index < value.array.size(); ++index)
            compare_typed_value((*item)->items[index], value.array[index], name + "[" + std::to_string(index) + "]");
    } else {
        throw std::runtime_error("Unsupported original typed oracle kind: " + type);
    }
}

void compare_typed_globals(const LslCheckpoint &state, const std::string &source, const network::Value &expected) {
    const auto names = global_names(source);
    check(names.size() == state.globals.size() && expected.map.size() == names.size(),
          "Original typed global declaration/oracle count differs from native VM");
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto found = expected.map.find(names[index]);
        check(found != expected.map.end(), "Original typed resume oracle omitted global " + names[index]);
        compare_typed_value(state.globals[index], found->second, names[index]);
    }
}

void compare_globals(const LslCheckpoint &state, const std::string &source, const network::Value &expected) {
    const auto names = global_names(source);
    check(names.size() == state.globals.size() && expected.map.size() == names.size(),
          "Original fixture global declaration/oracle count differs from native VM");
    std::map<std::string, std::size_t> indexes;
    for (std::size_t index = 0; index < names.size(); ++index) {
        indexes.emplace(names[index], index);
        const auto found = expected.map.find(names[index]);
        check(found != expected.map.end(), "Original resume oracle omitted global " + names[index]);
        if (found->second.kind == network::Value::Kind::Integer)
            check(std::get<std::int32_t>(state.globals[index]) == network::osd_integer(found->second),
                  "Resumed native global differs from unchanged original execution: " + names[index]);
        else
            check(list_text(state.globals[index]) == found->second.text,
                  "Resumed native list/string differs from unchanged original execution: " + names[index]);
    }
    if (indexes.contains("alias") && indexes.contains("shared"))
        check(std::get<std::shared_ptr<const LslList>>(state.globals[indexes.at("alias")]) ==
                  std::get<std::shared_ptr<const LslList>>(state.globals[indexes.at("shared")]),
              "Managed-to-native continuation lost the shared global list alias");
}

void compare_leaf_arguments(const LslCheckpoint &native, const YEngineSnapshot &original, const YEngineObjectCode &code,
                            const network::Value &metadata, const std::string &source) {
    const auto &leaf = original.managed_frames.back();
    const auto &slots = std::get<YEngineManagedArray>(leaf.slots->value);
    const auto names = global_names(source);
    bool found_method = false;
    std::set<std::size_t> checked_arguments;
    for (const auto &method : metadata.get("methods").array) {
        if (method.get("name").text != leaf.function)
            continue;
        found_method = true;
        for (const auto &saved : method.get("saved_slots").array) {
            const auto number = network::osd_integer(saved.get("storage_number"));
            if (saved.get("storage_kind").text != "argument" || number < 1)
                continue;
            const auto slot = static_cast<std::size_t>(network::osd_integer(saved.get("index")));
            const auto parameter = static_cast<std::size_t>(number - 1);
            check(slot < slots.size() && parameter < native.locals.size(),
                  "Original/native leaf parameter bounds differ");
            const auto &raw = slots[slot];
            check(bool(raw), "Original leaf parameter graph node is absent");
            if (raw->kind == YEngineManagedValue::Kind::Integer || raw->kind == YEngineManagedValue::Kind::LslInteger) {
                check(std::get<std::int32_t>(native.locals[parameter]) == std::get<std::int32_t>(raw->value),
                      "Restored leaf parameter lost its exact original loop-counter value before execution");
                checked_arguments.insert(parameter);
            } else if (raw->kind == YEngineManagedValue::Kind::LslList) {
                const auto &global_lists = std::get<YEngineManagedArray>(original.managed_globals[4]->value);
                bool matched_global = false;
                for (const auto &global : code.globals) {
                    if (global.type != "iarLists" || global.index < 0 ||
                        static_cast<std::size_t>(global.index) >= global_lists.size() ||
                        global_lists[global.index] != raw)
                        continue;
                    const auto found = std::find(names.begin(), names.end(), global.name);
                    check(found != names.end(), "Original shared-list global is absent from native declarations");
                    const auto index = static_cast<std::size_t>(found - names.begin());
                    check(index < native.globals.size(), "Native shared-list global index is out of bounds");
                    check(std::get<std::shared_ptr<const LslList>>(native.locals[parameter]) ==
                              std::get<std::shared_ptr<const LslList>>(native.globals[index]),
                          "Restored list argument lost its pointer alias to the original shared global");
                    matched_global = true;
                }
                check(matched_global, "Original list argument did not retain its shared-global graph node");
                checked_arguments.insert(parameter);
            } else {
                auto text = raw;
                while (text && (text->kind == YEngineManagedValue::Kind::LslString ||
                                text->kind == YEngineManagedValue::Kind::LslKey)) {
                    const auto child = std::get_if<YEngineManagedRef>(&text->value);
                    check(child != nullptr, "Original string/key argument wrapper has no graph reference");
                    text = *child;
                }
                check(text && text->kind == YEngineManagedValue::Kind::String,
                      "Original leaf parameter has an unsupported oracle type");
                const auto value = std::get_if<std::string>(&native.locals[parameter]);
                check(value && *value == std::get<std::string>(text->value),
                      "Restored leaf string/key parameter lost its exact original value before execution");
                checked_arguments.insert(parameter);
            }
        }
    }
    const auto method = code.methods.find(leaf.function);
    check(found_method && method != code.methods.end() && !method->second.arguments.empty(),
          "Original leaf parameter oracle did not expose its method declaration and instance argument");
    // Genuine post-API continuations can suspend in a zero-parameter function.
    // Keep the exact saved-argument check for every existing parameterized case.
    check(checked_arguments.size() == method->second.arguments.size() - 1,
          "Original leaf oracle did not verify every declared non-instance argument");
    for (std::size_t parameter = 0; parameter + 1 < method->second.arguments.size(); ++parameter)
        check(checked_arguments.contains(parameter), "Original leaf oracle omitted a declared argument");
}

void fixture(const std::filesystem::path &directory, bool supported) {
    const auto source = read(directory / "source.lsl");
    const auto snapshot_wire = read(directory / "snapshot.bin");
    const auto object_wire = read(directory / "original.yobj");
    const auto saved = decode_yengine_snapshot(bytes(snapshot_wire));
    const auto object = decode_yengine_object_code(bytes(object_wire));
    const auto metadata = network::parse_osd_json(read(directory / "metadata.json"));
    check(saved && object, "Original continuation input could not be decoded: " + directory.string());
    const auto program = compile_lsl(source);
    check(bool(program), "Original continuation fixture source did not compile natively: " + directory.string());
    std::int64_t clock = 10000000;
    unsigned chats = 0, sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.chat = [&](const auto &) { ++chats; };
    host.request_sleep = [&](int) { ++sleeps; };
    LslContext context;
    context.object = Uuid::random();
    context.owner = Uuid::random();
    context.item = Uuid::random();
    LslInstance live(program.program, context, host);
    const auto initial = encode_lsl_checkpoint(*live.checkpoint());
    const auto reject = [&](const YEngineSnapshot &candidate, const YEngineObjectCode &code, const std::string &hash) {
        check(!live.restore_yengine_snapshot(candidate, hash, source, code),
              "Unverified managed continuation was admitted");
        check(encode_lsl_checkpoint(*live.checkpoint()) == initial && chats == 0 && sleeps == 0,
              "Rejected managed continuation mutated the target VM or replayed an event");
    };
    if (!supported) {
        reject(*saved, *object, object->source_hash);
        return;
    }
    auto wrong_hash = object->source_hash;
    wrong_hash[0] = wrong_hash[0] == '0' ? '1' : '0';
    reject(*saved, *object, wrong_hash);
    auto invalid = *saved;
    invalid.managed_frames.back().function = "not-an-original-method";
    reject(invalid, *object, object->source_hash);
    invalid = *saved;
    invalid.managed_frames.back().call_no = std::numeric_limits<std::int32_t>::max();
    reject(invalid, *object, object->source_hash);
    invalid = *saved;
    auto slots = std::make_shared<YEngineManagedValue>(*invalid.managed_frames.back().slots);
    auto *values = std::get_if<YEngineManagedArray>(&slots->value);
    check(values != nullptr, "Original managed leaf frame slots are not an array");
    values->push_back(std::make_shared<YEngineManagedValue>());
    invalid.managed_frames.back().slots = slots;
    reject(invalid, *object, object->source_hash);
    auto wrong_object = *object;
    wrong_object.source_hash = wrong_hash;
    reject(*saved, wrong_object, object->source_hash);
    wrong_object = *object;
    wrong_object.methods.erase(saved->managed_frames.back().function);
    reject(*saved, wrong_object, object->source_hash);
    wrong_object = *object;
    wrong_object.methods.at(saved->managed_frames.back().function).records.clear();
    reject(*saved, wrong_object, object->source_hash);
    if (directory.filename().string().ends_with("_sleep")) {
        wrong_object = *object;
        bool corrupted = false;
        for (auto &record : wrong_object.methods.at(saved->managed_frames.back().function).records) {
            if (record.kind == YEngineObjectRecordKind::EmitExternalMethod && !record.strings.empty() &&
                record.strings.front().find("llSleep") != std::string::npos) {
                record.strings.front() = "llOwnerSay";
                corrupted = true;
            }
        }
        check(corrupted, "Original sleeping continuation did not expose its verified external API call");
        reject(*saved, wrong_object, object->source_hash);
    }
    if (directory.filename().string().ends_with("_api_return")) {
        wrong_object = *object;
        bool corrupted = false;
        for (auto &record : wrong_object.methods.at(saved->managed_frames.back().function).records) {
            if (record.kind == YEngineObjectRecordKind::EmitExternalMethod && !record.strings.empty() &&
                record.strings.front().find("llAbs") != std::string::npos) {
                record.strings.front() = "llOwnerSay";
                corrupted = true;
            }
        }
        check(corrupted, "Original API-return continuation did not expose its verified llAbs call");
        reject(*saved, wrong_object, object->source_hash);
    }
    const auto void_api = directory.filename() == "void_reset_time"  ? "llResetTime"
                          : directory.filename() == "void_set_timer" ? "llSetTimerEvent"
                          : directory.filename() == "void_profiler"  ? "llScriptProfiler"
                                                                     : "";
    if (*void_api) {
        wrong_object = *object;
        bool corrupted = false;
        for (auto &record : wrong_object.methods.at(saved->managed_frames.back().function).records) {
            if (record.kind == YEngineObjectRecordKind::EmitExternalMethod && !record.strings.empty() &&
                record.strings.front().find(void_api) != std::string::npos) {
                record.strings.front() = "llOwnerSay";
                corrupted = true;
            }
        }
        check(corrupted, "Original void continuation did not expose its verified API call");
        reject(*saved, wrong_object, object->source_hash);
    }

    LslInstance translated(program.program, context, host);
    check(translated.restore_yengine_snapshot(*saved, object->source_hash, source, *object),
          "Verified original managed continuation did not restore: " + directory.string());
    const auto admitted = translated.checkpoint();
    check(bool(admitted), "Translated managed continuation did not expose native admission state");
    check(!admitted->function.empty() && !admitted->call_stack.empty(),
          "Original managed frames were replayed without constructing suspended native call frames");
    compare_leaf_arguments(*admitted, *saved, *object, metadata, source);
    const auto fixture_name = directory.filename().string();
    constexpr std::int64_t reset_time_sentinel = 1234567;
    if (fixture_name == "void_reset_time") {
        // Plugin/time state is outside the raw managed stack. Replaying the
        // completed llResetTime must not erase preserved native elapsed time.
        auto timed = *admitted;
        timed.timer_elapsed = reset_time_sentinel;
        check(translated.restore_checkpoint(timed), "Could not seed the preserved elapsed-time state");
    }
    if (fixture_name == "void_set_timer")
        check(admitted->timer_interval == 0, "Raw managed snapshot unexpectedly contained a timer plugin envelope");
    const auto progress = translated.run(1);
    check(progress.status != LslRunStatus::Faulted && progress.status != LslRunStatus::Sleeping,
          "Original continuation failed before its first native checkpoint");
    const auto midway = translated.checkpoint();
    // A single native return instruction can legitimately finish the imported
    // leaf and unwind into the event. Admission above checks suspended frames
    // before execution; the resulting state must remain checkpointable.
    check(bool(midway), "Translated continuation did not expose its next native checkpoint");
    const auto native_wire = encode_lsl_checkpoint(*midway);
    const auto native_checkpoint = decode_lsl_checkpoint(native_wire);
    LslInstance restored(program.program, context, host);
    check(native_checkpoint && restored.restore_checkpoint(*native_checkpoint),
          "Translated continuation did not survive a fresh native VM checkpoint restore");
    finish(translated);
    finish(restored);
    if (fixture_name == "void_reset_time")
        check(translated.checkpoint()->timer_elapsed == reset_time_sentinel &&
                  restored.checkpoint()->timer_elapsed == reset_time_sentinel,
              "Imported void continuation replayed llResetTime and erased preserved elapsed time");
    if (fixture_name == "void_set_timer")
        check(translated.checkpoint()->timer_interval == 0 && restored.checkpoint()->timer_interval == 0,
              "Imported void continuation replayed llSetTimerEvent without an original plugin envelope");
    const auto typed = metadata.map.find("original_roundtrip_resume_typed_globals");
    check(!directory.filename().string().ends_with("_api") || typed != metadata.map.end(),
          "Typed API continuation fixture omitted its exact original typed global oracle");
    if (typed != metadata.map.end()) {
        compare_typed_globals(*translated.checkpoint(), source, typed->second);
        compare_typed_globals(*restored.checkpoint(), source, typed->second);
    } else {
        const auto &expected = metadata.get("original_roundtrip_resume_globals");
        compare_globals(*translated.checkpoint(), source, expected);
        compare_globals(*restored.checkpoint(), source, expected);
    }
    check(encode_lsl_checkpoint(*midway) == native_wire && chats == 0 && sleeps == 0,
          "Finishing continuation mutated its saved checkpoint or replayed startup side effects");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original continuation fixture directory required");
        const std::filesystem::path root = argv[1];
        for (const auto *name : {"nested",          "pending",        "list_alias",         "global_initializer",
                                 "nested_sleep",    "pending_sleep",  "integer_api_return", "nested_api_return",
                                 "float_api",       "string_api",     "list_api",           "vector_api",
                                 "rotation_api",    "key_api",        "key_argument_api",   "unicode_lambda",
                                 "unicode_cjk",     "unicode_emoji",  "unicode_mixed",      "unicode_crlf",
                                 "void_reset_time", "void_set_timer", "void_profiler",      "pending_changed"})
            fixture(root / name, true);
        fixture(root / "codec_types", false);
        std::cout << "PASS: original managed continuations resume without replay and preserve native checkpoints\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <map>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Missing original ordinary-text fixture: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void compare_value(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto &clr = expected.get("clr").text;
    if (clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLInteger") {
        const auto *value = std::get_if<LslBoxedInteger>(&actual);
        check(value && value->value == network::osd_integer(expected.get("value")),
              where + ": boxed LSLInteger differs");
        return;
    }
    if (clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString") {
        const auto *value = std::get_if<LslBoxedString>(&actual);
        check(value && value->value == expected.get("value").text, where + ": boxed LSLString differs");
        return;
    }
    if (expected.get("type").text == "float") {
        const double *value = nullptr;
        if (clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLFloat") {
            const auto *boxed = std::get_if<LslBoxedFloat>(&actual);
            check(boxed != nullptr, where + ": Boxed LSLFloat storage differs");
            value = &boxed->value;
        } else
            value = std::get_if<double>(&actual);
        const auto &text = expected.get("bits").text;
        std::uint64_t bits{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
        check(value && text.size() == 16 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                  std::bit_cast<std::uint64_t>(*value) == bits,
              where + ": Raw/boxed Float bits differ");
        return;
    }
    if (expected.get("type").text == "key") {
        const auto *value = std::get_if<LslBoxedKey>(&actual);
        check(value && value->value == expected.get("value").text, where + ": Raw backend boxed key differs");
        return;
    }
    const auto type = expected.get("type").text;
    if (type == "string") {
        const auto *value = std::get_if<std::string>(&actual);
        check(value && *value == expected.get("value").text, where + ": String storage/value differs");
    } else if (type == "integer") {
        const auto *value = std::get_if<std::int32_t>(&actual);
        check(value && *value == network::osd_integer(expected.get("value")), where + ": Int32 storage/value differs");
    } else if (type == "list") {
        const auto *value = std::get_if<std::shared_ptr<const LslList>>(&actual);
        check(value && *value && (*value)->items.size() == expected.get("value").array.size(),
              where + ": List differs");
        for (std::size_t i = 0; i < (*value)->items.size(); ++i)
            compare_value((*value)->items[i], expected.get("value").array[i], where + "[" + std::to_string(i) + "]");
    } else if (type == "vector" || type == "rotation") {
        std::vector<double> components;
        if (type == "vector") {
            const auto *v = std::get_if<framework::Vector3d>(&actual);
            check(v != nullptr, where + ": Vector storage differs");
            components = {v->x, v->y, v->z};
        } else {
            const auto *v = std::get_if<LslRotation>(&actual);
            check(v != nullptr, where + ": Rotation storage differs");
            components = {v->x, v->y, v->z, v->w};
        }
        check(components.size() == expected.get("value").array.size(), where + ": Component count differs");
        for (std::size_t i = 0; i < components.size(); ++i) {
            const auto &text = expected.get("value").array[i].get("bits").text;
            std::uint64_t bits{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
            check(text.size() == 16 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                      std::bit_cast<std::uint64_t>(components[i]) == bits,
                  where + ": IEEE component " + std::to_string(i) + " bits differ; actual=" +
                      std::to_string(std::bit_cast<std::uint64_t>(components[i])) + " original=" + text);
        }
    } else
        throw std::runtime_error(where + ": Unreviewed original truth global type " + type + " CLR " + clr);
}

std::vector<std::string> globals(std::string source) {
    // Ignore function/event bodies and quoted text rather than treating a
    // function's local marker as an additional global declaration.
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (char &ch : source) {
        const char original = ch;
        if (quoted) {
            ch = ' ';
            if (escaped)
                escaped = false;
            else if (original == '\\')
                escaped = true;
            else if (original == '"')
                quoted = false;
            continue;
        }
        if (original == '"') {
            quoted = true;
            ch = ' ';
        } else if (original == '{') {
            ++depth;
            ch = ' ';
        } else if (original == '}') {
            --depth;
            ch = ' ';
        } else if (depth)
            ch = ' ';
    }
    const std::regex declaration(
        R"(\b(?:integer|float|string|key|vector|rotation|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> result;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        result.push_back((*it)[1].str());
    return result;
}

void compare_globals(const LslCheckpoint &checkpoint, const std::string &source, const network::Value &expected) {
    const auto names = globals(source);
    check(names.size() == checkpoint.globals.size() && names.size() == expected.map.size(),
          "Original global declaration count differs");
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto found = expected.map.find(names[i]);
        check(found != expected.map.end(), "Original global omitted " + names[i]);
        compare_value(checkpoint.globals[i], found->second, names[i]);
    }
}

LslValue typed_input(const network::Value &node) {
    const auto &type = node.get("type").text;
    const auto &clr = node.get("clr").text;
    if (type == "float") {
        const auto &text = node.get("bits").text;
        std::uint64_t bits{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
        check(text.size() == 16 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
              "Original injected float bits malformed");
        const auto value = std::bit_cast<double>(bits);
        if (clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLFloat")
            return LslBoxedFloat{value};
        check(clr == "System.Double", "Unreviewed original input float storage");
        return value;
    }
    if (type == "string") {
        if (clr == "System.String")
            return node.get("value").text;
        check(clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString", "Unreviewed input string storage");
        return LslBoxedString{node.get("value").text};
    }
    if (type == "key")
        return LslBoxedKey{node.get("value").text};
    if (type == "list") {
        auto value = std::make_shared<LslList>();
        for (const auto &item : node.get("value").array)
            value->items.push_back(typed_input(item));
        return std::shared_ptr<const LslList>(value);
    }
    throw std::runtime_error("Unreviewed original injected input type " + type);
}

void fixture(const std::filesystem::path &root, const network::Value &oracle,
             const network::Value *chat_proof = nullptr) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    const auto compiled = compile_lsl(source);
    if (!oracle.get("accepted").boolean) {
        check(!compiled, "Original compile-rejected ordinary cast was accepted");
        check(!compiled.diagnostics.empty(), "Rejected cast has no diagnostic");
        return;
    }
    check(yengine_source_matches(source, oracle.get("source_hash").text), "Original source hash mismatch");
    std::string errors;
    for (const auto &e : compiled.diagnostics)
        errors += " " + e.message;
    check(bool(compiled), "Accepted original ordinary text source did not compile:" + errors);
    check(oracle.get("initial_exception").kind != network::Value::Kind::Map,
          "Original text source faulted unexpectedly");
    std::int64_t clock = 638000000000000000LL;
    std::vector<LslChat> chats;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.chat = [&](const LslChat &chat) { chats.push_back(chat); };
    LslContext context;
    context.owner = framework::Uuid::random();
    context.group = framework::Uuid::random();
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    LslInstance instance(compiled.program, context, host);
    if (oracle.get("injected_event_arguments").kind == network::Value::Kind::Map) {
        auto payload = typed_input(oracle.get("injected_event_arguments"));
        const auto &items = std::get<std::shared_ptr<const LslList>>(payload)->items;
        check(instance.post_event({"touch_start", items}), "Original arbitrary PostEvent arguments were rejected");
    }
    bool complete = false;
    for (unsigned step = 0; step < 1000; ++step) {
        const auto result = instance.run(1);
        check(result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
              "Ordinary text conversion faulted/slept: " + result.error);
        auto saved = instance.checkpoint();
        check(bool(saved), "Ordinary text checkpoint unavailable");
        if (result.status == LslRunStatus::BudgetExhausted) {
            auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*saved));
            LslInstance fresh(compiled.program, context, host);
            check(decoded && fresh.restore_checkpoint(*decoded), "Ordinary text fresh-VM continuation rejected");
            instance = std::move(fresh);
        }
        if (result.status == LslRunStatus::Idle) {
            complete = true;
            break;
        }
    }
    check(complete, "Ordinary text source exceeded instruction bound");
    const auto saved = instance.checkpoint();
    check(bool(saved), "Final ordinary text checkpoint unavailable");
    compare_globals(*saved, source, oracle.get("final").get("globals"));
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*saved));
    LslInstance fresh(compiled.program, context, host);
    check(decoded && fresh.restore_checkpoint(*decoded), "Final ordinary text fresh-VM restore rejected");
    const auto restored = fresh.checkpoint();
    check(bool(restored), "Restored text checkpoint missing");
    compare_globals(*restored, source, oracle.get("final").get("globals"));
    if (name == "implicit_owner_say_list") {
        check(chats.size() == 1 && chats.front().type == LslChatType::Owner && chats.front().channel == 0,
              "Implicit list OwnerSay count/type/channel differs");
        if (chat_proof) {
            const auto &original = chat_proof->get("observed_scene_chats").array;
            check(original.size() == 1 && original.front().get("chat_type").text == "Owner" &&
                      network::osd_integer(original.front().get("channel")) == 0 &&
                      chats.front().text == original.front().get("text").text,
                  "Actual original Scene OwnerSay text differs");
        }
    } else
        check(chats.empty(), "Pure ordinary conversion produced unexpected chat");
}

void sdk_region_stats(const std::filesystem::path &path) {
    const auto wire = read(path);
    check(framework::sha256_hex(wire) == "8775a80e0dba9645e17a919c76ca7f53bc60ba843a6039b015cff6b118ccbf65",
          "Original SDK producer metadata SHA differs");
    const auto proof = network::parse_osd_json(wire);
    check(proof.get("method").text == "osGetRegionStats" && proof.get("values").array.size() == 6,
          "Original SDK proof schema differs");
    std::vector<float> input;
    for (const auto &row : proof.get("values").array) {
        const auto &text = row.get("source_bits").text;
        std::uint32_t bits{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
        check(text.size() == 8 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                  row.get("source_type").text == "System.Single",
              "Original SDK Single bits malformed");
        input.push_back(std::bit_cast<float>(bits));
    }
    check(std::bit_cast<std::uint32_t>(static_cast<float>(5e-7)) == std::bit_cast<std::uint32_t>(input[3]),
          "Host5e-7 Single narrowing differs");
    const auto program = compile_lsl(
        R"LSL(list values;string dumped;string csv;default{state_entry(){values=osGetRegionStats();dumped=llDumpList2String(values,"|");csv=llList2CSV(values);}})LSL");
    check(bool(program), "SDK statistics source failed compile");
    std::size_t queries = 0;
    LslHost host;
    host.ossl_permission_error = [](auto, auto, std::string_view name) {
        check(name == "osGetRegionStats", "SDK permission identity differs");
        return std::string{};
    };
    host.region_stats = [&] {
        ++queries;
        return input;
    };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    LslInstance instance(program.program, context, host);
    bool complete = false;
    for (unsigned step = 0; step < 1000; ++step) {
        const auto result = instance.run(1);
        check(result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
              "SDK script failed: " + result.error);
        if (result.status == LslRunStatus::Idle) {
            complete = true;
            break;
        }
        const auto saved = instance.checkpoint();
        check(bool(saved), "SDK checkpoint unavailable");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*saved));
        LslInstance fresh(program.program, context, host);
        check(decoded && fresh.restore_checkpoint(*decoded), "SDK fresh-VM checkpoint rejected");
        instance = std::move(fresh);
    }
    const auto saved = instance.checkpoint();
    check(complete && saved && queries == 1, "SDK producer replayed or failed");
    const auto *list = std::get_if<std::shared_ptr<const LslList>>(&saved->globals[0]);
    check(list && *list && (*list)->items.size() == 6, "SDK producer list differs");
    for (std::size_t i = 0; i < 6; ++i) {
        const auto *actual = std::get_if<LslBoxedFloat>(&(*list)->items[i]);
        const auto &row = proof.get("values").array[i];
        const auto &text = row.get("bits").text;
        std::uint64_t bits{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
        check(actual && row.get("returned_clr").text == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLFloat" &&
                  text.size() == 16 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                  std::bit_cast<std::uint64_t>(actual->value) == bits,
              "SDK boxed Float bits differ at " + std::to_string(i));
    }
    check(std::get<std::string>(saved->globals[1]) == proof.get("dumped").text &&
              std::get<std::string>(saved->globals[2]) == proof.get("csv").text,
          "Original SDK Dump/CSV text differs");
}

void sdk_physics_json(const std::filesystem::path &path) {
    const auto wire = read(path);
    check(framework::sha256_hex(wire) == "4f8f3d27e5a9c9b2045e76902957e18bf9b497ca6de4366eba210673fa0d4f4e",
          "Original physics/JSON SDK proof SHA differs");
    const auto proof = network::parse_osd_json(wire);
    check(proof.get("json").array.size() == 18 && proof.get("physics").get("host_property_inputs").array.size() == 4,
          "Original physics/JSON SDK proof counts differ");
    std::array<float, 4> material{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto &text = proof.get("physics").get("host_property_inputs").array[i].get("bits").text;
        std::uint32_t bits{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
        check(text.size() == 8 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
              "Original material Single bits malformed");
        material[i] = std::bit_cast<float>(bits);
    }
    const auto execute = [&](const std::string &expression, const network::Value &expected, bool physics) {
        const auto source = "list values;string dumped;string csv;default{state_entry(){values=" + expression +
                            ";dumped=llDumpList2String(values,\"|\");csv=llList2CSV(values);}}";
        const auto program = compile_lsl(source);
        check(bool(program), "SDK control source failed compile: " + expression);
        unsigned queries = 0;
        LslHost host;
        host.physics_material = [&](auto) {
            ++queries;
            return material;
        };
        LslContext context;
        context.object = framework::Uuid::random();
        context.item = framework::Uuid::random();
        LslInstance instance(program.program, context, host);
        bool complete = false;
        for (unsigned step = 0; step < 1000; ++step) {
            const auto result = instance.run(1);
            check(result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
                  "SDK control faulted: " + expression + ": " + result.error);
            if (result.status == LslRunStatus::Idle) {
                complete = true;
                break;
            }
            const auto saved = instance.checkpoint();
            check(bool(saved), "SDK control checkpoint missing");
            const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*saved));
            LslInstance fresh(program.program, context, host);
            check(decoded && fresh.restore_checkpoint(*decoded), "SDK control checkpoint failed restore");
            instance = std::move(fresh);
        }
        const auto saved = instance.checkpoint();
        check(complete && saved && queries == (physics ? 1u : 0u), "SDK control replayed or failed");
        compare_value(saved->globals[0], expected.get("typed"), expression);
        check(std::get<std::string>(saved->globals[1]) == expected.get("dumped").text &&
                  std::get<std::string>(saved->globals[2]) == expected.get("csv").text,
              "SDK Dump/CSV differs: " + expression);
    };
    execute("llGetPhysicsMaterial()", proof.get("physics").get("returned"), true);
    for (const auto &row : proof.get("json").array) {
        check(row.get("exception").kind != network::Value::Kind::Map, "Unreviewed original JSON exception");
        execute("llJson2List(" + network::osd_json(network::Value(row.get("input").text)) + ")", row.get("result"),
                false);
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc >= 2 && argc <= 5, "Expected ordinary text fixture and optional actual Scene chat proof directory");
        const std::filesystem::path root(argv[1]);
        const auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 94, "Original ordinary text corpus count differs");
        std::optional<network::Value> chat_proof;
        if (argc >= 3) {
            const std::filesystem::path directory(argv[2]);
            chat_proof = network::parse_osd_json(read(directory / "metadata.json"));
            const auto source = read(directory / "source.lsl");
            check(source == read(root / "implicit_owner_say_list" / "source.lsl") &&
                      yengine_source_matches(source, chat_proof->get("source_hash").text) &&
                      chat_proof->get("accepted").boolean &&
                      chat_proof->get("initial_exception").kind != network::Value::Kind::Map,
                  "Actual Scene chat proof source/provenance differs");
        }
        std::size_t accepted = 0, rejected = 0, matrix_accepted = 0, matrix_rejected = 0, implicit = 0;
        for (const auto &row : rows.array) {
            const auto name = row.get("name").text;
            const bool valid = row.get("accepted").boolean;
            (valid ? accepted : rejected)++;
            if (name.starts_with("matrix_"))
                (valid ? matrix_accepted : matrix_rejected)++;
            if (name.starts_with("implicit_"))
                ++implicit;
            try {
                fixture(root, row, chat_proof ? &*chat_proof : nullptr);
            } catch (const std::exception &e) {
                throw std::runtime_error(name + ": " + e.what());
            }
        }
        check(accepted == 76 && rejected == 18 && matrix_accepted == 35 && matrix_rejected == 14 && implicit == 10,
              "Original ordinary text accepted/rejected/matrix/implicit accounting differs");
        if (argc >= 4)
            sdk_region_stats(std::filesystem::path(argv[3]));
        if (argc == 5)
            sdk_physics_json(std::filesystem::path(argv[4]));
        std::cout << "PASS: 94 original ordinary text cases (76 execution, 18 rejection; explicit matrix35/14; "
                     "implicit10), exact UTF8/raw and boxed values across fresh-VM checkpoints\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

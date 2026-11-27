// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/script/yengine_xml_values.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <memory>
#include <unordered_set>
using namespace utf16_test;

namespace {
std::u16string units(const network::Value &node) {
    std::u16string text;
    for (const auto &unit : node.array)
        text.push_back(static_cast<char16_t>(network::osd_integer(unit)));
    return text;
}

double real(const network::Value &node) {
    return std::bit_cast<double>(std::stoull(node.get("bits").text, nullptr, 16));
}

LslValue value(const network::Value &node) {
    const auto type = node.get("type").text, clr = node.get("clr").text;
    if (type == "null" || clr.empty())
        return LslNullObject{};
    if (clr == "System.Boolean")
        return LslRawBoolean{node.get("value").boolean || node.get("value").text == "True"};
    if (clr == "System.Single")
        return LslRawSingle{
            std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(node.get("bits").text, nullptr, 16)))};
    if (clr == "OpenMetaverse.UUID")
        return LslRawUuid{framework::Uuid(node.get("value").text)};
    if (type == "integer" || clr == "System.Int32" || clr.ends_with("+LSLInteger"))
        return clr.ends_with("+LSLInteger") ? LslValue(LslBoxedInteger{network::osd_integer(node.get("value"))})
                                            : LslValue(network::osd_integer(node.get("value")));
    if (type == "float" || clr == "System.Double" || clr.ends_with("+LSLFloat"))
        return clr.ends_with("+LSLFloat") ? LslValue(LslBoxedFloat{real(node)}) : LslValue(real(node));
    if (type == "char" || clr == "System.Char")
        return LslCharacter{static_cast<char16_t>(network::osd_integer(node.get("value")))};
    if (type == "string" || clr == "System.String" || clr.ends_with("+LSLString")) {
        bool boxed = clr.ends_with("+LSLString");
        if (node.get("utf16").kind != network::Value::Kind::Array)
            return boxed ? LslValue(LslBoxedNullString{}) : LslValue(LslNullString{});
        return boxed ? box_lsl_string(units(node.get("utf16"))) : make_lsl_string(units(node.get("utf16")));
    }
    if (type == "key" || clr.ends_with("+key"))
        return LslBoxedKey{node.get("value").text};
    if (type == "vector") {
        const auto &a = node.get("value").array;
        return framework::Vector3d{real(a.at(0)), real(a.at(1)), real(a.at(2))};
    }
    if (type == "rotation") {
        const auto &a = node.get("value").array;
        return LslRotation{real(a.at(0)), real(a.at(1)), real(a.at(2)), real(a.at(3))};
    }
    if (type == "list") {
        auto list = std::make_shared<LslList>();
        for (const auto &item : node.get("value").array)
            list->items.push_back(value(item));
        return std::shared_ptr<const LslList>(list);
    }
    throw std::runtime_error("Unreviewed queue value " + type + "/" + clr);
}

void same(const LslValue &a, const LslValue &b, const std::string &where) {
    if (lsl_string_code_units(a) || is_null_lsl_string(a)) {
        check(lsl_string_code_units(a) == lsl_string_code_units(b) && string_identity(a) == string_identity(b),
              where + ": String units/boxing differ");
        return;
    }
    check(a.index() == b.index(), where + ": carrier differs");
    if (const auto x = std::get_if<std::shared_ptr<const LslList>>(&a)) {
        auto y = std::get<std::shared_ptr<const LslList>>(b);
        check(*x && y && (*x)->items.size() == y->items.size(), where + ": List shape differs");
        for (std::size_t n = 0; n < y->items.size(); ++n)
            same((*x)->items[n], y->items[n], where + "/" + std::to_string(n));
        return;
    }
    const auto bits = [&](double x, double y) {
        check(std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y), where + ": IEEE differs");
    };
    if (const auto x = std::get_if<double>(&a)) {
        bits(*x, std::get<double>(b));
        return;
    }
    if (const auto x = std::get_if<LslBoxedFloat>(&a)) {
        bits(x->value, std::get<LslBoxedFloat>(b).value);
        return;
    }
    if (const auto x = std::get_if<framework::Vector3d>(&a)) {
        auto y = std::get<framework::Vector3d>(b);
        bits(x->x, y.x);
        bits(x->y, y.y);
        bits(x->z, y.z);
        return;
    }
    if (const auto x = std::get_if<LslRotation>(&a)) {
        auto y = std::get<LslRotation>(b);
        bits(x->x, y.x);
        bits(x->y, y.y);
        bits(x->z, y.z);
        bits(x->w, y.w);
        return;
    }
    if (const auto x = std::get_if<LslRawSingle>(&a)) {
        check(std::bit_cast<std::uint32_t>(x->value) == std::bit_cast<std::uint32_t>(std::get<LslRawSingle>(b).value),
              where + ": Single differs");
        return;
    }
    if (const auto x = std::get_if<std::int32_t>(&a)) {
        check(*x == std::get<std::int32_t>(b), where + ": Integer differs");
        return;
    }
    if (const auto x = std::get_if<LslBoxedInteger>(&a)) {
        check(x->value == std::get<LslBoxedInteger>(b).value, where + ": boxedInteger differs");
        return;
    }
    if (const auto x = std::get_if<LslCharacter>(&a)) {
        check(x->value == std::get<LslCharacter>(b).value, where + ": Char differs");
        return;
    }
    if (const auto x = std::get_if<LslRawBoolean>(&a)) {
        check(x->value == std::get<LslRawBoolean>(b).value, where + ": Boolean differs");
        return;
    }
    if (const auto x = std::get_if<LslRawUuid>(&a)) {
        check(x->value == std::get<LslRawUuid>(b).value, where + ": UUID differs");
        return;
    }
    if (std::holds_alternative<LslNullObject>(a) || std::holds_alternative<LslNullList>(a))
        return;
    throw std::runtime_error(where + ": unreviewed primitive identity");
}

std::vector<LslValue> arguments(const network::Value &queue) {
    check(queue.array.size() == 1, "Original queue count differs");
    std::vector<LslValue> result;
    for (const auto &item : queue.array.at(0).get("arguments").array)
        result.push_back(value(item));
    return result;
}

void compare(const std::vector<LslValue> &a, const std::vector<LslValue> &b, const std::string &where) {
    check(a.size() == b.size(), where + ": count differs");
    for (std::size_t n = 0; n < a.size(); ++n)
        same(a[n], b[n], where + "/" + std::to_string(n));
}

std::span<const std::uint8_t> bytes(const std::string &text) {
    return {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()};
}

std::string envelope(std::string queue, std::string snapshot, std::string hash) {
    return "<ScriptState Engine=\"YEngine\" SourceHash=\"" + hash + "\"><Running>True</Running><Snapshot>" + snapshot +
           "</Snapshot>" + queue + "</ScriptState>";
}

std::string object_xml(const scene::ManagedScriptState &state, framework::Uuid asset, framework::Uuid item) {
    scene::ObjectAssetPart part;
    part.object.id = framework::Uuid::random();
    framework::TaskInventoryItem script;
    script.item_id = item;
    script.asset_id = asset;
    script.type = script.inventory_type = 10;
    script.parent_id = script.parent_part_id = part.object.id;
    script.name = "Original queued arguments";
    part.inventory.push_back(script);
    part.managed_script_states.emplace(item, state);
    auto sink = std::make_shared<framework::NiniStringWriter>();
    framework::NativeXmlTextWriter writer(framework::native_xml_string_writer_io(sink), true);
    scene::write_object_asset_xml(writer, part, {});
    writer.close();
    return framework::utf8(*sink->to_string());
}

void verify_import(const std::string &xml, const std::vector<LslValue> &expected) {
    auto asset = framework::Uuid::random(), item = framework::Uuid::random();
    auto state = scene::read_yengine_state_xml(bytes(xml), asset);
    check(state && state->events.size() == 1, "Direct queued XML import failed");
    compare(state->events.at(0).arguments, expected, "Direct XML");
    auto serialized = object_xml(*state, asset, item);
    auto object = scene::read_object_asset_xml(bytes(serialized));
    check(object && object->linksets.size() == 1, "Embedded queued XML import failed");
    const auto &saved = object->linksets.at(0).parts.at(0).managed_script_states.at(item);
    compare(saved.events.at(0).arguments, expected, "Embedded XML");
    auto again = scene::read_yengine_state_xml(bytes(saved.original_xml), asset);
    check(again && again->events.size() == 1, "Opaque retained XML reimport failed");
    compare(again->events.at(0).arguments, expected, "Opaque XML");
    auto appended = saved;
    check(scene::append_managed_lifecycle_events(appended,
                                                 std::array<LslEvent, 1>{LslEvent{"changed", {std::int32_t{256}}, {}}}),
          "Lifecycle append failed");
    auto reloaded = scene::read_yengine_state_xml(bytes(appended.original_xml), asset);
    check(reloaded && reloaded->events.size() == 2, "Lifecycle XML reimport lost events");
    compare(reloaded->events.at(0).arguments, expected, "Lifecycle retained queue");
}

void source_case(const std::filesystem::path &root, const network::Value &row) {
    auto folder = root / row.get("name").text;
    auto name = row.get("name").text;
    for (const auto *file : {"source.lsl", "original.yobj", "original.il.txt", "snapshot.bin"})
        check(framework::sha256_hex(read(folder / file)) == row.get("sha256").get(file).text,
              "Original queue artifact changed");
    auto initial = arguments(row.get("initial").get("queue"));
    // Recreate only the alias explicitly witnessed in the genuine original.
    if (name == "nested_alias")
        initial.at(1) = initial.at(0);
    LslCheckpoint c;
    c.program_hash = row.get("source_hash").text;
    c.state = "default";
    c.queued_events = {{"touch_start", initial, {}}};
    if (name == "nested_alias") {
        c.global_initialization_pending = false;
        c.globals = {initial.at(0)};
        c.active_event = LslEvent{"state_entry", {initial.at(0)}, {}};
        LslResumeFrameCheckpoint frame;
        frame.method = "xmrEventDequeue";
        frame.event_wait = LslEventWaitCheckpoint{1, 0, 0, 0, 1, 0};
        frame.background_event = LslEvent{"touch_start", {initial.at(0)}, {}};
        c.resume_frames = {frame};
    }
    auto internal = encode_lsl_checkpoint(c);
    auto copied = decode_lsl_checkpoint(internal);
    check(copied && encode_lsl_checkpoint(*copied) == internal, "Internal queue codec changed bytes");
    compare(copied->queued_events.at(0).arguments, initial, "Internal queue");
    bool failed = false;
    std::vector<LslValue> normalized;
    try {
        normalized = normalize_yengine_xml_event_arguments(initial);
    } catch (const std::exception &error) {
        failed = true;
        const auto expected = row.get("queue_xml_write_exception").get("type").text;
        check(
            (expected == "System.NullReferenceException" &&
             std::string(error.what()).starts_with("NullReferenceException:")) ||
                (expected == "System.ArgumentException" && std::string(error.what()).starts_with("ArgumentException:")),
            "XML failure category differs");
    }
    const bool expected_failure = row.get("queue_xml_write_exception").kind == network::Value::Kind::Map;
    check(failed == expected_failure, "Queue XML failure stage differs");
    if (!failed) {
        auto saved = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
        check(saved.has_value(), "Persistent queued checkpoint decode failed");
        auto expected = arguments(row.get("after_load").get("queue"));
        compare(saved->queued_events.at(0).arguments, expected, "Persistent queued arguments");
        if (name == "nested_alias") {
            auto global = std::get<std::shared_ptr<const LslList>>(saved->globals.at(0));
            check(global == std::get<std::shared_ptr<const LslList>>(saved->active_event->arguments.at(0)) &&
                      global == std::get<std::shared_ptr<const LslList>>(
                                    saved->resume_frames.at(0).background_event->arguments.at(0)),
                  "XML queue copy broke global/active/binary BG aliases");
            same(saved->globals.at(0), initial.at(0), "Unchanged non-XML List");
            check(global != std::get<std::shared_ptr<const LslList>>(saved->queued_events.at(0).arguments.at(0)),
                  "XML queue still aliases binary/global storage");
        }
        compare(normalized, expected, "Original XML normalization");
        if (name == "nested_alias")
            check(std::get<std::shared_ptr<const LslList>>(normalized.at(0)) !=
                      std::get<std::shared_ptr<const LslList>>(normalized.at(1)),
                  "XML retained original List alias");
        const auto raw = read(folder / "snapshot.bin");
        const auto encoded = network::binary_base64(
            network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(raw.begin(), raw.end())));
        verify_import(envelope(read(folder / "event-queue.xml"), encoded, row.get("source_hash").text), expected);
    }
    if (name == "raw_backend_control")
        return; // This row is explicitly a real backend control, not invented source production.
    const auto source = read(folder / "source.lsl");
    auto compiled = compile_lsl(source);
    check(bool(compiled), "Original queue source rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.request_sleep = [&](int) { ++sleeps; };
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool slept = false;
    for (unsigned step = 0; step < 2000; ++step) {
        auto run = instance->run(1);
        check(run.status != LslRunStatus::Faulted, "Queue source faulted before its real sleep: " + run.error);
        auto cp = instance->checkpoint();
        check(cp.has_value(), "Queue source checkpoint missing");
        if (run.status == LslRunStatus::Sleeping && cp->sleep_remaining > 0) {
            check(sleeps == 1, name + ": source sleep API call count differs");
            compare(cp->queued_events.at(0).arguments, initial, "Actual source queue");
            slept = true;
            break;
        }
        auto restored = decode_lsl_checkpoint(encode_lsl_checkpoint(*cp));
        auto next = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(restored && next->restore_checkpoint(*restored), "Queue source budget restore failed");
        instance = std::move(next);
    }
    check(slept, name + ": Queue source did not reach original sleep");
    const auto resume = [&](LslInstance &script, const network::Value &expected) {
        auto checkpoint = script.checkpoint();
        check(checkpoint.has_value(), "Sleeping queue checkpoint missing");
        clock += checkpoint->sleep_remaining + 1;
        for (unsigned step = 0; step < 2000; ++step) {
            auto outcome = script.run(1);
            if (outcome.status == LslRunStatus::Faulted) {
                check(expected.kind == network::Value::Kind::Map &&
                          outcome.error.find(expected.get("message").text) != std::string::npos,
                      "Queue dequeue/assignment failure stage differs: " + outcome.error);
                return;
            }
            if (outcome.status == LslRunStatus::Idle) {
                check(expected.kind != network::Value::Kind::Map,
                      "Expected original dequeue/assignment fault was lost");
                return;
            }
        }
        throw std::runtime_error("Queue source resume exceeded bound");
    };
    std::unique_ptr<LslInstance> transferred;
    if (!expected_failure) {
        auto projection = instance->persistent_checkpoint();
        check(projection.has_value(), "Queue source persistence projection missing");
        auto restored = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(*projection));
        transferred = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(restored && transferred->restore_checkpoint(*restored), "Actual queued source external restore failed");
    }
    resume(*instance, row.get("live_resume_exception"));
    if (transferred)
        resume(*transferred, row.get("resume_exception"));
    check(sleeps == 1, "Queue source sleep API replayed across persistence");
}

void reload_cases(const std::filesystem::path &root, const std::filesystem::path &source_root,
                  std::size_t expected_count, std::string_view expected_hash) {
    auto metadata = read(root / "metadata.json");
    check(framework::sha256_hex(metadata) == expected_hash, "Original reload metadata changed");
    auto oracle = network::parse_osd_json(metadata);
    check(oracle.get("cases").array.size() == expected_count, "Original reload count differs");
    auto source_rows = network::parse_osd_json(read(source_root / "metadata.json"));
    auto hash = source_rows.array.at(0).get("source_hash").text;
    auto raw = read(source_root / source_rows.array.at(0).get("name").text / "snapshot.bin");
    auto encoded = network::binary_base64(
        network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(raw.begin(), raw.end())));
    for (const auto &row : oracle.get("cases").array) {
        auto folder = root / row.get("name").text;
        const bool failure = row.get("append_exception").kind == network::Value::Kind::Map ||
                             row.get("outer_xml_exception").kind == network::Value::Kind::Map;
        if (failure) {
            bool rejected = false;
            try {
                normalize_yengine_xml_event_arguments(std::array<LslValue, 1>{value(row.get("input"))});
            } catch (const std::exception &error) {
                rejected = std::string(error.what())
                               .starts_with(row.get("name").text == "raw_null" ? "NullReferenceException:"
                                                                               : "ArgumentException:");
            }
            check(rejected, "Original rejected XML primitive was accepted");
            continue;
        }
        auto xml = read(folder / "serialized.xml");
        check(framework::sha256_hex(xml) == row.get("sha256").get("serialized.xml").text,
              "Original serialized reload XML changed");
        const std::vector<LslValue> original_input{value(row.get("input"))};
        const std::vector<LslValue> original_restored{value(row.get("restored"))};
        compare(normalize_yengine_xml_event_arguments(original_input), original_restored,
                "Original XML primitive reparse");
        verify_import(envelope(xml, encoded, hash), {value(row.get("restored"))});
    }
    verify_import(envelope("<EventQueue><Event Name=\"touch_start\"><param "
                           "type=\"Missing.Original.Type\">opaque</param></Event></EventQueue>",
                           encoded, hash),
                  {LslNullObject{}});
    // The unchanged original accepts this DAG and reloads 1,048,575
    // independent Lists containing 1,048,576 Int32 leaves (61,866,020 XML bytes).
    LslValue graph = std::int32_t{1};
    for (unsigned n = 0; n < 20; ++n)
        graph = std::make_shared<const LslList>(LslList{{graph, graph}});
    const auto expanded = normalize_yengine_xml_event_arguments(std::array<LslValue, 1>{graph});
    check(expanded.size() == 1, "Expanded XML graph root differs");
    std::vector<const LslValue *> pending{&expanded[0]};
    std::unordered_set<const LslList *> identities;
    identities.reserve((1u << 20) - 1);
    std::size_t leaves = 0;
    while (!pending.empty()) {
        const auto *value = pending.back();
        pending.pop_back();
        if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(value)) {
            check(*list && (*list)->items.size() == 2 && identities.insert(list->get()).second,
                  "Queued XML graph retained a List alias or changed its arity");
            for (const auto &item : (*list)->items)
                pending.push_back(&item);
        } else {
            const auto *integer = std::get_if<std::int32_t>(value);
            check(integer && *integer == 1, "Queued XML graph changed a leaf");
            ++leaves;
        }
    }
    check(identities.size() == (1u << 20) - 1 && leaves == (1u << 20),
          "Queued XML graph expansion differs from original");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 4, "Expected source12, reload21 and integer10 fixture directories");
        auto root = std::filesystem::path(argv[1]);
        auto text = read(root / "metadata.json");
        check(framework::sha256_hex(text) == "ecfad9b32676680262fc8ca8e728d8211c6405b524af8794a3d53cd8343e404a",
              "Original queue metadata changed");
        auto rows = network::parse_osd_json(text);
        check(rows.array.size() == 12, "Original queue count differs");
        for (const auto &row : rows.array) {
            try {
                source_case(root, row);
            } catch (const std::exception &error) {
                throw std::runtime_error(row.get("name").text + ": " + error.what());
            }
        }
        reload_cases(std::filesystem::path(argv[2]), root, 21,
                     "61fe223e394c7b0f7cfcc160ab75945ca9279a6e578fef980de278d1fc7bc63a");
        reload_cases(std::filesystem::path(argv[3]), root, 10,
                     "c20307732818afc9733dd9f8f301942202659db9937068943b178e258766c039");
        std::cout << "Original queued argument source12/reload21/integer10/import checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

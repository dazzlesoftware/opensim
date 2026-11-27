// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/script/yengine_xml_values.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <pugixml.hpp>

using namespace utf16_test;

namespace {
std::int32_t integer(const LslValue &value) {
    const auto *payload = &value;
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(payload)) {
        check(*box && valid_lsl_object_box(**box), "Invalid queued Object box");
        payload = &(*box)->value;
    }
    const auto unboxed = unbox_lsl_value(*payload);
    const auto *result = std::get_if<std::int32_t>(&unboxed);
    check(result != nullptr, "Expected queued Int32 cell");
    return *result;
}

std::shared_ptr<const LslList> list(const LslValue &value) {
    const auto *result = std::get_if<std::shared_ptr<const LslList>>(&value);
    check(result && *result, "Expected queued List");
    return *result;
}

std::string list_hash(const LslValue &value) {
    std::string cells;
    const auto items = list(value);
    cells.reserve(items->items.size() * 4);
    for (const auto &item : items->items) {
        const auto bits = std::bit_cast<std::uint32_t>(integer(item));
        for (unsigned index = 0; index < 4; ++index)
            cells.push_back(static_cast<char>((bits >> (index * 8)) & 255));
    }
    return framework::sha256_hex(cells);
}

LslValue make_list(std::size_t count) {
    auto result = std::make_shared<LslList>();
    result->items.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result->items.emplace_back(LslBoxedInteger{index + 1 == count ? -7 : 19});
    return std::shared_ptr<const LslList>(result);
}

std::string string_hash(const LslValue &value) {
    const auto units = lsl_string_code_units(value);
    check(units.has_value(), "Expected queued String");
    return framework::sha256_hex(framework::utf8(*units));
}

void native_list(std::size_t count) {
    const auto input = make_list(count);
    const auto expected = list_hash(input);
    const std::array<LslValue, 2> arguments{input, input};
    const auto referenced = reference_yengine_xml_event_arguments(arguments);
    check(list(referenced[0]) == list(referenced[1]), "Reference promotion lost pre-XML List alias");
    const auto normalized = normalize_yengine_xml_event_arguments(referenced);
    check(normalized.size() == 2 && list(normalized[0])->items.size() == count &&
              list_hash(normalized[0]) == expected && list_hash(normalized[1]) == expected,
          "Large queued List normalization changed complete cells");
    check(list(normalized[0]) != list(normalized[1]) && list(normalized[0]) != list(input),
          "XML List copies retained reference aliases");

    LslCheckpoint checkpoint;
    checkpoint.program_hash = "queue-capacity-list";
    checkpoint.state = "default";
    checkpoint.globals = {input};
    checkpoint.active_event = LslEvent{"state_entry", {input}, {}};
    checkpoint.queued_events = {{"touch_start", {input, input}, {}}, {"touch_start", {input}, {}}};
    const auto wire = encode_persistent_lsl_checkpoint(checkpoint);
    const auto restored = decode_lsl_checkpoint(wire);
    check(restored && restored->queued_events.size() == 2 && restored->active_event,
          "Large queued List persistent checkpoint rejected");
    const auto global = list(restored->globals[0]);
    const auto first = list(restored->queued_events[0].arguments[0]);
    const auto second = list(restored->queued_events[0].arguments[1]);
    const auto third = list(restored->queued_events[1].arguments[0]);
    check(global == list(restored->active_event->arguments[0]) && first != global && first != second &&
              first != third && second != third,
          "Persistent XML queue did not split aliases independently of binary storage");
    for (const auto &event : restored->queued_events)
        for (const auto &argument : event.arguments)
            check(list(argument)->items.size() == count && list_hash(argument) == expected,
                  "Persistent queued List changed full cells");

    auto malformed = encode_lsl_checkpoint(checkpoint);
    std::array<std::uint8_t, 5> header{5, 0, 0, 0, 0};
    for (unsigned index = 0; index < 4; ++index)
        header[index + 1] = static_cast<std::uint8_t>((count >> (index * 8)) & 255);
    const auto at = std::search(malformed.begin(), malformed.end(), header.begin(), header.end());
    check(at != malformed.end(), "Native queued List length header missing");
    std::fill(at + 1, at + 5, 255);
    check(!decode_lsl_checkpoint(malformed), "Impossible queued List length accepted");
    auto truncated = wire;
    truncated.resize(truncated.size() / 2);
    check(!decode_lsl_checkpoint(truncated), "Truncated persistent queue accepted");
}

void native_string() {
    const std::string text(4 * 1024 * 1024 + 1, 'x');
    const auto expected = framework::sha256_hex(text);
    for (const bool boxed : {false, true}) {
        const auto input = boxed ? LslValue{LslBoxedString{text}} : LslValue{text};
        const std::array<LslValue, 1> arguments{input};
        const auto normalized = normalize_yengine_xml_event_arguments(arguments);
        check(normalized.size() == 1 && string_hash(normalized[0]) == expected,
              "Long queued String normalization changed contents");
        const auto promoted = reference_yengine_xml_event_arguments(arguments);
        const auto normalized_reference = normalize_yengine_xml_event_arguments(promoted);
        check(normalized_reference.size() == 1 && string_hash(normalized_reference[0]) == expected,
              "Long queued reference String normalization changed contents");
        LslCheckpoint checkpoint;
        checkpoint.program_hash = "queue-capacity-string";
        checkpoint.state = "default";
        checkpoint.queued_events = {{"touch_start", {input}, {}}};
        const auto wire = encode_persistent_lsl_checkpoint(checkpoint);
        const auto restored = decode_lsl_checkpoint(wire);
        check(wire.size() > 4 * 1024 * 1024 && restored && restored->queued_events.size() == 1 &&
                  string_hash(restored->queued_events[0].arguments[0]) == expected,
              "Long queued String persistent checkpoint changed contents");
        check(std::holds_alternative<LslBoxedString>(restored->queued_events[0].arguments[0]) == boxed,
              "Long queued String boxing changed");
    }
    bool rejected = false;
    try {
        const std::array<LslValue, 1> invalid{LslUtf16String{std::u16string(1, char16_t{0xd800})}};
        (void)normalize_yengine_xml_event_arguments(invalid);
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid surrogate accepted during queue XML normalization");
}

void verify_summary(const LslValue &value, const network::Value &expected) {
    const auto count = static_cast<std::size_t>(network::osd_integer(expected.get("length")));
    if (expected.get("kind").text == "list") {
        const auto items = list(value);
        check(items->items.size() == count && list_hash(value) == expected.get("cellsSha256").text,
              "Original queued List full-cell summary differs");
        for (const auto &item : items->items)
            check(std::holds_alternative<LslBoxedInteger>(item), "Original SDK List cell boxing differs");
    } else {
        const auto units = lsl_string_code_units(value);
        check(units && units->size() == count && string_hash(value) == expected.get("cellsSha256").text,
              "Original queued String full-cell summary differs");
        check(std::holds_alternative<LslBoxedString>(value) == expected.get("clr").text.ends_with("+LSLString"),
              "Original queued String CLR boxing differs");
    }
}

void original_cases(const std::filesystem::path &root) {
    const auto proof_text = read(root / "verification.json");
    check(framework::sha256_hex(proof_text) == "d734f0c9c865bbd51fcb495af8f1619b559917489db9c5884053964614fc0e62",
          "Original queue-capacity proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    for (const auto &[path, digest] : proof.get("files").map)
        check(framework::sha256_hex(read(root / path)) == digest.text, "Original queue artifact changed: " + path);
    unsigned cases = 0;
    for (const auto platform : {"windows", "linux"}) {
        const auto rows = network::parse_osd_json(read(root / platform / "metadata.json"));
        check(rows.array.size() == 4, "Original queue-capacity inventory differs");
        for (const auto &row : rows.array) {
            check(row.get("postAccepted").boolean && network::osd_integer(row.get("queuedCount")) == 1 &&
                      network::osd_integer(row.get("restoredCount")) == 1,
                  "Original did not admit/write/restore queued capacity control");
            const auto count = static_cast<std::size_t>(network::osd_integer(row.get("length")));
            std::vector<LslValue> arguments;
            if (row.get("kind").text == "list") {
                arguments = {make_list(count)};
            } else {
                std::string text(count, 'x');
                text.front() = 'A';
                text[count / 2] = 'B';
                text.back() = 'Z';
                const bool boxed = row.get("before").get("clr").text.ends_with("+LSLString");
                arguments = {LslBoxedInteger{0}, LslBoxedString{"queue"},
                             LslBoxedKey{"11111111-2222-3333-4444-555555555555"},
                             boxed ? LslValue{LslBoxedString{text}} : LslValue{text}};
            }
            verify_summary(arguments.back(), row.get("before"));
            const auto normalized = normalize_yengine_xml_event_arguments(arguments);
            check(normalized.size() == arguments.size(), "Original normalization argument count differs");
            verify_summary(normalized.back(), row.get("after"));
            const auto xml = read(root / platform / row.get("name").text / "event-queue.xml");
            check(framework::sha256_hex(xml) == row.get("xmlSha256").text, "Original queue XML changed");
            const auto envelope = "<ScriptState Engine=\"YEngine\"><Running>True</Running>" + xml + "</ScriptState>";
            pugi::xml_document document;
            check(bool(document.load_buffer(envelope.data(), envelope.size())), "Original queued XML invalid");
            const auto imported = scene::read_managed_script_state(document.document_element());
            check(imported.events.size() == 1 && imported.events[0].name == row.get("eventName").text &&
                      imported.events[0].arguments.size() == arguments.size(),
                  "Original queue XML import changed event/arguments");
            verify_summary(imported.events[0].arguments.back(), row.get("after"));
            LslCheckpoint checkpoint;
            checkpoint.program_hash = "original-queue-capacity";
            checkpoint.state = "default";
            checkpoint.queued_events = imported.events;
            const auto restored = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(checkpoint));
            check(restored && restored->queued_events.size() == 1, "Original queue persistent checkpoint rejected");
            verify_summary(restored->queued_events[0].arguments.back(), row.get("after"));
            ++cases;
        }
    }
    check(cases == 8, "Missing original queue capacity cases");
}

} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original queue-capacity fixture directory");
        native_list(65537);
        native_list(100000);
        native_string();
        original_cases(argv[1]);
        std::cout << "PASS original queue capacities, complete cell/string hashes, XML alias splitting, "
                     "persistent queued checkpoints and malformed payloads\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

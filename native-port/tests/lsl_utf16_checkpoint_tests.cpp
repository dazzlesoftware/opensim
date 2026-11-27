// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_json.hpp"
#include <filesystem>
#include <fstream>
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

LslCheckpoint base() {
    LslCheckpoint c;
    c.program_hash = "character-source";
    c.state = "default";
    c.globals = {std::int32_t{1}};
    c.initialized_globals = 1;
    return c;
}

void legacy() {
    for (unsigned version = 1; version <= 14; ++version) {
        auto c = base();
        if (version == 2 || version == 6) {
            LslListenerCheckpoint l{};
            l.handle = 1;
            l.regex_flags = version == 6 ? 1 : 0;
            c.listeners = {l};
        }
        if (version == 3)
            c.permissions_mask = 1;
        if (version == 4)
            c.start_parameter = 1;
        if (version == 5) {
            LslSensorQuery q{};
            q.interval = 1.;
            c.sensors = {q};
        }
        if (version == 7) {
            auto l = std::make_shared<const LslList>(LslList{{std::int32_t{7}}});
            c.globals = {l, l};
        }
        if (version == 8)
            c.function = "worker:0,";
        if (version == 9)
            c.event_wait = LslEventWaitCheckpoint{};
        if (version == 10)
            c.event_wait = LslEventWaitCheckpoint{1, 0, 1, 0, 2, 0};
        if (version == 11)
            c.globals = {LslBoxedInteger{7}};
        if (version == 12)
            c.global_initialization_pending = false;
        if (version == 13) {
            c.global_initialization_pending = false;
            c.active_event = LslEvent{"state_entry", {}, {}};
            c.pending_resume_fault = "fault";
        }
        if (version == 14)
            c.globals = {LslCharacter{0xd800}};
        const auto wire = encode_lsl_checkpoint(c);
        check(wire[3] == version, "Legacy version changed");
        const auto d = decode_lsl_checkpoint(wire);
        check(d && encode_lsl_checkpoint(*d) == wire, "Legacy bytes changed on roundtrip");
    }
}

void units() {
    const std::array<std::u16string, 5> inputs{u"", u"valid ASCII", std::u16string{0},
                                               std::u16string{0xd800, 0, 0xdc00, 0xffff},
                                               std::u16string{0xd83d, 0xde00}};
    for (const auto &input : inputs)
        for (unsigned kind = 0; kind < 3; ++kind) {
            auto c = base();
            if (kind == 0)
                c.globals = {LslUtf16String{input}};
            if (kind == 1)
                c.globals = {LslBoxedUtf16String{input}};
            if (kind == 2)
                c.globals = {LslBoxedUtf16Key{input}};
            const auto wire = encode_lsl_checkpoint(c);
            check(wire[3] == 15, "UTF16 alternative did not select15");
            const auto d = decode_lsl_checkpoint(wire);
            check(d && d->globals[0].index() == 11 + kind && d->globals[0] == c.globals[0] &&
                      encode_lsl_checkpoint(*d) == wire,
                  "Raw UTF16 variant/unit identity changed");
            check(!d->global_initialization_pending, "Minimal UTF16 CP invented initializer flag");
            const auto tag = 4 + 4 + c.program_hash.size() + 4 + c.state.size() + 4 + c.next_state.size() + 4;
            check(wire[tag] == 12 + kind, "UTF16 alternative wire tag differs");
            for (std::size_t n = 0; n < wire.size(); ++n)
                check(!decode_lsl_checkpoint(std::span(wire).first(n)), "Truncated UTF16 CP accepted");
            for (unsigned v = 1; v <= 14; ++v) {
                auto bad = wire;
                bad[3] = v;
                check(!decode_lsl_checkpoint(bad), "Old version accepted UTF16 alternative");
            }
            auto bad = wire;
            bad[tag] = 15;
            check(!decode_lsl_checkpoint(bad), "Unknown UTF16 value tag accepted");
            bad = wire;
            for (unsigned i = 1; i <= 4; ++i)
                bad[tag + i] = 255;
            check(!decode_lsl_checkpoint(bad), "Oversized/truncated UTF16 unit count accepted");
            bad = wire;
            bad.push_back(0);
            check(!decode_lsl_checkpoint(bad), "UTF16 trailing data accepted");
            bad = wire;
            bad[3] = 16;
            check(!decode_lsl_checkpoint(bad), "Unknown future version accepted");
        }
    auto c = base();
    c.globals = {LslUtf16String{std::u16string(65537, u'x')}};
    auto wire = encode_lsl_checkpoint(c);
    const auto d = decode_lsl_checkpoint(wire);
    check(d && std::get<LslUtf16String>(d->globals[0]).value.size() == 65537,
          "String unit count incorrectly limited by collection entry count");
    c.globals = {LslUtf16String{std::u16string(2 * 1024 * 1024 + 1, u'x')}};
    const auto large_wire = encode_lsl_checkpoint(c);
    const auto large_decoded = decode_lsl_checkpoint(large_wire);
    check(large_wire.size() > 4 * 1024 * 1024 && large_decoded &&
              std::get<LslUtf16String>(large_decoded->globals[0]).value == std::get<LslUtf16String>(c.globals[0]).value,
          "Large UTF16 checkpoint failed roundtrip");
    c = base();
    c.global_initialization_pending = false;
    c.active_event = LslEvent{"state_entry", {}, {}};
    c.pending_resume_fault = "fault";
    auto bad = encode_lsl_checkpoint(c);
    bad[3] = 15;
    check(!decode_lsl_checkpoint(bad), "Noncanonical UTF16-free15 accepted");
    c = base();
    c.globals = {LslCharacter{0xd800}};
    bad = encode_lsl_checkpoint(c);
    bad[3] = 15;
    check(!decode_lsl_checkpoint(bad), "Character alone falsely satisfies UTF16 schema15");
}

void roots() {
    for (const LslValue ch : std::array<LslValue, 3>{LslUtf16String{std::u16string{0xd800, 0}},
                                                     LslBoxedUtf16String{std::u16string{0xdc00, 0}},
                                                     LslBoxedUtf16Key{std::u16string{0xffff, 0}}}) {
        for (unsigned root = 0; root < 17; ++root) {
            auto c = base();
            c.global_initialization_pending = false;
            c.active_event = LslEvent{"state_entry", {}, {}};
            LslCallFrameCheckpoint f;
            f.function = "caller:0,";
            LslCapturePointCheckpoint p;
            p.evaluations = {{7, ch}};
            LslEventContextCheckpoint e;
            e.active_event = {"touch_start", {}, {}};
            e.event_wait = LslEventWaitCheckpoint{9, 0, 0, 0, 1, 0};
            switch (root) {
            case 0:
                c.globals = {ch};
                break;
            case 1:
                c.locals = {ch};
                break;
            case 2:
                c.queued_events = {{"touch_start", {ch}, {}}};
                break;
            case 3:
                c.active_event->arguments = {ch};
                break;
            case 4:
                f.locals = {ch};
                c.call_stack = {f};
                break;
            case 5:
                f.evaluations = {{1, ch}};
                c.call_stack = {f};
                break;
            case 6:
                c.evaluations = {{1, ch}};
                break;
            case 7:
                c.capture_point = p;
                break;
            case 8:
                f.capture_point = p;
                c.call_stack = {f};
                break;
            case 9:
                e.locals = {ch};
                c.event_contexts = {e};
                break;
            case 10:
                e.active_event.arguments = {ch};
                c.event_contexts = {e};
                break;
            case 11:
                e.evaluations = {{1, ch}};
                c.event_contexts = {e};
                break;
            case 12:
                f.locals = {ch};
                e.call_stack = {f};
                c.event_contexts = {e};
                break;
            case 13:
                e.capture_point = p;
                c.event_contexts = {e};
                break;
            case 14: {
                LslResumeFrameCheckpoint r;
                r.method = "worker";
                r.locals = {ch};
                r.complete_restore = true;
                c.resume_frames = {r};
                break;
            }
            case 15: {
                LslResumeFrameCheckpoint r;
                r.method = "worker";
                r.evaluations = {{1, ch}};
                r.complete_restore = true;
                c.resume_frames = {r};
                break;
            }
            case 16:
                e.kind = LslEventContextKind::DirectHandler;
                e.event_wait.reset();
                e.direct_call_expression = 1;
                e.invoked_event = LslEvent{"touch_end", {ch}, {}};
                c.event_contexts = {e};
                break;
            }
            const auto wire = encode_lsl_checkpoint(c, false);
            check(wire[3] == 15, "Hidden UTF16 root did not promote version15");
            const auto d = decode_lsl_checkpoint(wire);
            check(d && encode_lsl_checkpoint(*d, false) == wire, "Hidden character root roundtrip differs");
        }
        auto c = base();
        auto child = std::make_shared<const LslList>(LslList{{ch, LslBoxedInteger{3}}});
        auto outer = std::make_shared<const LslList>(LslList{{child, child}});
        c.globals = {outer};
        c.locals = {child};
        const auto wire = encode_lsl_checkpoint(c);
        const auto d = decode_lsl_checkpoint(wire);
        check(d && wire[3] == 15, "Nested UTF16 decode failed");
        const auto l = std::get<std::shared_ptr<const LslList>>(d->globals[0]);
        check(std::get<std::shared_ptr<const LslList>>(l->items[0]) ==
                      std::get<std::shared_ptr<const LslList>>(l->items[1]) &&
                  std::get<std::shared_ptr<const LslList>>(d->locals[0]) ==
                      std::get<std::shared_ptr<const LslList>>(l->items[0]),
              "Nested UTF16 DAG aliases changed");
    }
}

void detector_names_and_persistent_boundary() {
    const std::u16string wide{u'a', 0xd800, 0, u'b'};
    LslDetector detector;
    detector.name = "legacy fallback";
    detector.sdk_name = wide;
    for (unsigned root = 0; root < 5; ++root) {
        auto c = base();
        c.global_initialization_pending = false;
        c.active_event = LslEvent{"state_entry", {}, {}};
        LslEventContextCheckpoint outer;
        outer.active_event = {"touch_start", {}, {}};
        outer.event_wait = LslEventWaitCheckpoint{9, 0, 0, 0, 1, 0};
        if (root == 0)
            c.active_event->detectors = {detector};
        if (root == 1)
            c.queued_events = {{"touch_start", {}, {detector}}};
        if (root == 2) {
            outer.active_event.detectors = {detector};
            c.event_contexts = {outer};
        }
        if (root == 3) {
            outer.kind = LslEventContextKind::DirectHandler;
            outer.event_wait.reset();
            outer.direct_call_expression = 1;
            outer.invoked_event = LslEvent{"touch_end", {}, {detector}};
            c.event_contexts = {outer};
        }
        if (root == 4) {
            LslResumeFrameCheckpoint frame;
            frame.method = "xmrEventDequeue";
            frame.event_wait = LslEventWaitCheckpoint{9, 0, 0, 0, 1, 0};
            frame.background_event = LslEvent{"touch_start", {}, {detector}};
            frame.complete_restore = false;
            c.resume_frames = {frame};
        }
        const auto internal = encode_lsl_checkpoint(c);
        check(internal[3] == 15, "Wide detector alone did not select15");
        const auto decoded = decode_lsl_checkpoint(internal);
        check(decoded && encode_lsl_checkpoint(*decoded) == internal, "Wide detector did not roundtrip exactly");
        for (std::size_t length = 0; length < internal.size(); ++length)
            check(!decode_lsl_checkpoint(std::span(internal).first(length)), "Truncated detector UTF16 accepted");
        const std::array<std::uint8_t, 13> suffix{1, 4, 0, 0, 0, 97, 0, 0, 216, 0, 0, 98, 0};
        const auto at = std::search(internal.begin(), internal.end(), suffix.begin(), suffix.end());
        check(at != internal.end(), "UTF16 detector suffix flag/unit framing differs");
        auto malformed = internal;
        malformed[static_cast<std::size_t>(at - internal.begin())] = 2;
        check(!decode_lsl_checkpoint(malformed), "Invalid UTF16 detector presence flag accepted");
        bool rejected = false;
        std::optional<LslCheckpoint> persisted;
        try {
            persisted = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).starts_with("ArgumentException: ");
        }
        if (root == 4) {
            // Original xmrEventDequeue slot7 is binary SDK data, independently
            // verified by the phase21 old/new72 oracle. Its String writer
            // replaces lone units; this is not the DetectArray XML boundary.
            check(!rejected && persisted.has_value(), "Binary BG detector name was rejected");
            const auto &restored = persisted->resume_frames.at(0).background_event->detectors.at(0);
            const auto name =
                restored.sdk_name ? LslValue(LslUtf16String{*restored.sdk_name}) : LslValue(restored.name);
            check(lsl_string_code_units(name) == std::optional<std::u16string>(std::u16string{u'a', 0xfffd, 0, u'b'}),
                  "Binary BG detector String replacement differs");
        } else {
            check(rejected, "External detector XML boundary admitted isolated surrogate");
        }
        check(encode_lsl_checkpoint(c) == internal, "Persistent detector writer mutated live input");
    }
    // External original BinaryWriter String boundaries replace isolated units;
    // internal wire, including raw well-formed UTF16 alternatives, remains exact.
    auto c = base();
    auto child = std::make_shared<const LslList>(
        LslList{{LslUtf16String{wide}, LslBoxedUtf16String{wide}, LslBoxedUtf16Key{wide}, LslCharacter{0xffff}}});
    c.globals = {child, child};
    c.locals = {child};
    const auto internal = encode_lsl_checkpoint(c);
    const auto persisted = encode_persistent_lsl_checkpoint(c);
    check(persisted[3] == 14, "Persistent wide strings plus character must select14");
    const auto d = decode_lsl_checkpoint(persisted);
    check(bool(d), "Persistent string graph rejected");
    const auto list = std::get<std::shared_ptr<const LslList>>(d->globals[0]);
    check(list == std::get<std::shared_ptr<const LslList>>(d->globals[1]) &&
              list == std::get<std::shared_ptr<const LslList>>(d->locals[0]),
          "Persistent string projection detached list aliases");
    const auto normalized = opensim::framework::utf8(wide);
    check(std::get<std::string>(list->items[0]) == normalized &&
              std::get<LslBoxedString>(list->items[1]).value == normalized &&
              std::get<LslBoxedKey>(list->items[2]).value == normalized &&
              std::get<LslCharacter>(list->items[3]).value == 0xffff,
          "Persistent string boxing or independent character policy changed");
    check(encode_lsl_checkpoint(c) == internal, "Persistent writer mutated live list graph");
    c = base();
    c.globals = {LslUtf16String{wide}};
    check(encode_persistent_lsl_checkpoint(c)[3] == 1, "Normalized plain wide string did not recover legacy1");
    c.globals = {LslBoxedUtf16String{wide}};
    check(encode_persistent_lsl_checkpoint(c)[3] == 11, "Normalized boxed wide string did not recover boxed11");
}

void original_string_migration(const std::filesystem::path &root) {
    const auto read = [](const std::filesystem::path &p) {
        std::ifstream stream(p, std::ios::binary);
        check(bool(stream), "Original string migration fixture missing");
        return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    const auto rows = opensim::network::parse_osd_json(read(root / "metadata.json"));
    check(rows.array.size() == 3, "Original production string migration case count differs");
    const auto units = [](const opensim::network::Value &v) {
        std::u16string text;
        for (const auto &x : v.get("utf16").array)
            text += static_cast<char16_t>(opensim::network::osd_integer(x));
        return text;
    };
    for (const auto &row : rows.array) {
        const auto source = read(root / row.get("name").text / "source.lsl");
        check(yengine_source_matches(source, row.get("source_hash").text) && row.get("accepted").boolean &&
                  row.get("migrated").boolean &&
                  row.get("production_write_exception").kind != opensim::network::Value::Kind::Map,
              "Original production String source/provenance failed");
        const auto live = units(row.get("initial").get("globals").get("global"));
        const auto expected = units(row.get("final").get("globals").get("global"));
        auto c = base();
        auto list = std::make_shared<const LslList>(LslList{{LslBoxedUtf16String{live}}});
        c.globals = {LslUtf16String{live}, list};
        const auto internal = encode_lsl_checkpoint(c);
        const auto preserved = decode_lsl_checkpoint(internal);
        check(preserved && std::get<LslUtf16String>(preserved->globals[0]).value == live,
              "Internal String state lost actual original live units");
        const auto migrated = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
        check(migrated && opensim::framework::utf16_replacing(std::get<std::string>(migrated->globals[0])) == expected,
              "Persistent String boundary differs from actual original MigrateOut");
        const auto boxed = std::get<std::shared_ptr<const LslList>>(migrated->globals[1]);
        check(opensim::framework::utf16_replacing(std::get<LslBoxedString>(boxed->items[0]).value) ==
                  units(row.get("final").get("globals").get("items").get("value").array[0]),
              "Persistent boxed String differs from actual original migration");
        check(encode_lsl_checkpoint(c) == internal, "Original String boundary test mutated live graph");
    }
}

void original_external_failures(const std::filesystem::path &characters,
                                const std::optional<std::filesystem::path> &detectors) {
    const auto read = [](const std::filesystem::path &p) {
        std::ifstream f(p, std::ios::binary);
        check(bool(f), "Original external boundary fixture missing");
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    const auto rows = opensim::network::parse_osd_json(read(characters / "metadata.json"));
    check(rows.array.size() == 8, "Original SYSCHAR migration count differs");
    unsigned failures = 0;
    for (const auto &row : rows.array) {
        const auto source = read(characters / row.get("name").text / "source.lsl");
        check(yengine_source_matches(source, row.get("source_hash").text) && row.get("accepted").boolean,
              "Original SYSCHAR provenance failed");
        const auto program = compile_lsl(source);
        check(bool(program), "Original SYSCHAR source failed compile");
        LslHost host;
        host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{638000000000000000LL}; };
        host.request_sleep = [](int) {};
        LslInstance vm(program.program, {}, host);
        check(vm.run(1000).status == LslRunStatus::Sleeping, "Original SYSCHAR source did not reach sleep");
        const auto saved = vm.checkpoint();
        check(bool(saved), "SYSCHAR checkpoint missing");
        const auto live = encode_lsl_checkpoint(*saved);
        check(decode_lsl_checkpoint(live).has_value(), "Internal SYSCHAR lost surrogate units");
        bool rejected = false;
        std::string error;
        try {
            encode_persistent_lsl_checkpoint(*saved);
        } catch (const std::runtime_error &e) {
            rejected = true;
            error = e.what();
        }
        const auto &expected = row.get("production_write_exception");
        check(rejected == (expected.kind == opensim::network::Value::Kind::Map),
              "Original SYSCHAR production exception differs");
        if (rejected) {
            ++failures;
            check(expected.get("type").text == "System.ArgumentException" &&
                      error == "ArgumentException: " + expected.get("message").text,
                  "Original SYSCHAR exception category/message differs");
        }
        check(encode_lsl_checkpoint(*saved) == live, "SYSCHAR external writer mutated live state");
    }
    check(failures == 6, "Original SYSCHAR failure accounting differs");
    if (!detectors)
        return;
    const auto names = opensim::network::parse_osd_json(read(*detectors / "metadata.json"));
    check(names.array.size() == 5, "Original detector XML count differs");
    unsigned rejected_count = 0;
    for (const auto &row : names.array) {
        const auto source = read(*detectors / row.get("name").text / "source.lsl");
        check(yengine_source_matches(source, row.get("source_hash").text) && row.get("accepted").boolean,
              "Original detector source provenance failed");
        std::u16string name;
        for (const auto &unit : row.get("initial").get("detectors").array.front().get("name_utf16").array)
            name += static_cast<char16_t>(opensim::network::osd_integer(unit));
        auto c = base();
        LslDetector detector;
        detector.sdk_name = name;
        c.active_event = LslEvent{"state_entry", {}, {detector}};
        const auto live = encode_lsl_checkpoint(c);
        bool rejected = false;
        try {
            encode_persistent_lsl_checkpoint(c);
        } catch (const std::runtime_error &e) {
            rejected = std::string(e.what()).starts_with("ArgumentException: ");
        }
        const auto &expected = row.get("detect_xml_write_exception");
        check(rejected == (expected.kind == opensim::network::Value::Kind::Map),
              "Full external detector XML failure differs from actual original");
        if (rejected)
            ++rejected_count;
        check(encode_lsl_checkpoint(c) == live, "Detector external writer mutated live state");
    }
    check(rejected_count == 3, "Original detector XML failure accounting differs");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc >= 1 && argc <= 4, "Optional original production String migration fixture directory expected");
        legacy();
        units();
        roots();
        detector_names_and_persistent_boundary();
        if (argc >= 2)
            original_string_migration(std::filesystem::path(argv[1]));
        if (argc >= 3)
            original_external_failures(std::filesystem::path(argv[2]),
                                       argc == 4 ? std::optional<std::filesystem::path>{argv[3]} : std::nullopt);
        std::cout << "PASS: UTF16 schema15, legacy1-14 bytes, exact raw variants/code units, 17 hidden roots per "
                     "alternative, DAG aliases and malformed controls\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

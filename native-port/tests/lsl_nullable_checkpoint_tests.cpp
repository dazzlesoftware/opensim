// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/framework/text.hpp"
#include <array>
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
    for (unsigned version = 1; version <= 15; ++version) {
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
        if (version == 15)
            c.globals = {LslUtf16String{u"well formed but explicit wide"}};
        const auto wire = encode_lsl_checkpoint(c);
        check(wire[3] == version, "Legacy version changed");
        const auto d = decode_lsl_checkpoint(wire);
        check(d && encode_lsl_checkpoint(*d) == wire, "Legacy bytes changed on roundtrip");
    }
}

void roots() {
    for (const LslValue ch : std::array<LslValue, 3>{LslNullString{}, LslBoxedNullString{}, LslBoxedNullKey{}}) {
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
            check(wire[3] == 16, "Hidden nullable root did not promote version16");
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
        check(d && wire[3] == 16, "Nested nullable decode failed");
        const auto l = std::get<std::shared_ptr<const LslList>>(d->globals[0]);
        check(std::get<std::shared_ptr<const LslList>>(l->items[0]) ==
                      std::get<std::shared_ptr<const LslList>>(l->items[1]) &&
                  std::get<std::shared_ptr<const LslList>>(d->locals[0]) ==
                      std::get<std::shared_ptr<const LslList>>(l->items[0]),
              "Nested nullable DAG aliases changed");
    }
}

void nullable_values() {
    for (unsigned kind = 0; kind < 3; ++kind) {
        auto c = base();
        const std::array<LslValue, 3> values{LslNullString{}, LslBoxedNullString{}, LslBoxedNullKey{}};
        c.globals = {values[kind], std::string{}, LslBoxedString{std::string{}}, LslBoxedKey{std::string{}}};
        auto wire = encode_lsl_checkpoint(c);
        check(wire[3] == 16, "Nullable values did not select16");
        const auto d = decode_lsl_checkpoint(wire);
        check(d && d->globals == c.globals && encode_lsl_checkpoint(*d) == wire,
              "Null/empty or raw/boxed identity changed");
        check(encode_persistent_lsl_checkpoint(c) == wire, "Persistent boundary changed null values");
        check(is_null_lsl_string(c.globals[0]) && !lsl_string_code_units(c.globals[0]), "Null became empty units");
        check(!is_null_lsl_string(c.globals[1]) && lsl_string_code_units(c.globals[1])->empty(), "Empty became null");
        check(std::holds_alternative<LslNullString>(unbox_lsl_value(c.globals[0])), "Null unboxing differs");
        const auto tag = 4 + 4 + c.program_hash.size() + 4 + c.state.size() + 4 + c.next_state.size() + 4;
        check(wire[tag] == 15 + kind, "Nullable tag differs");
        for (std::size_t n = 0; n < wire.size(); ++n)
            check(!decode_lsl_checkpoint(std::span(wire).first(n)), "Truncated nullable checkpoint accepted");
        for (unsigned v = 1; v <= 15; ++v) {
            auto bad = wire;
            bad[3] = v;
            check(!decode_lsl_checkpoint(bad), "Legacy schema accepted nullable tag");
        }
        auto bad = wire;
        bad[tag] = 18;
        check(!decode_lsl_checkpoint(bad), "Unknown nullable tag accepted");
        bad = wire;
        bad.push_back(0);
        check(!decode_lsl_checkpoint(bad), "Trailing nullable bytes accepted");
    }
    auto c = base();
    c.globals = {LslUtf16String{u"not null"}};
    auto wire = encode_lsl_checkpoint(c);
    wire[3] = 16;
    check(!decode_lsl_checkpoint(wire), "Noncanonical null-free16 accepted");
    auto list = std::make_shared<const LslList>(
        LslList{{LslNullString{}, LslBoxedNullString{}, LslBoxedNullKey{}, std::string{}}});
    c = base();
    c.globals = {list, list};
    c.locals = {list};
    const auto before = c.globals;
    for (bool persistent : {false, true}) {
        const auto bytes = persistent ? encode_persistent_lsl_checkpoint(c) : encode_lsl_checkpoint(c);
        const auto d = decode_lsl_checkpoint(bytes);
        check(bool(d), "Nullable DAG failed decode");
        const auto a = std::get<std::shared_ptr<const LslList>>(d->globals[0]);
        check(a == std::get<std::shared_ptr<const LslList>>(d->globals[1]) &&
                  a == std::get<std::shared_ptr<const LslList>>(d->locals[0]),
              "Nullable DAG aliases lost");
        check(a->items == list->items && c.globals == before, "Nullable DAG values changed or source mutated");
    }
    c = base();
    c.globals = {LslCharacter{0xd800}, LslNullString{}};
    check(decode_lsl_checkpoint(encode_lsl_checkpoint(c)).has_value(), "Internal char/null lost");
    bool rejected = false;
    try {
        encode_persistent_lsl_checkpoint(c);
    } catch (const std::exception &e) {
        rejected = std::string(e.what()).starts_with("ArgumentException:");
    }
    check(rejected, "Persistent surrogate char policy changed");
    c = base();
    c.globals = {LslUtf16String{std::u16string{0xd800}}, LslNullString{}};
    const auto d = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
    check(d && std::get<std::string>(d->globals[0]) == opensim::framework::utf8(std::u16string{0xfffd}) &&
              std::holds_alternative<LslNullString>(d->globals[1]),
          "String replacement/null preservation boundary changed");
}
} // namespace

int main() {
    try {
        legacy();
        roots();
        nullable_values();
        std::cout << "PASS: nullable16,legacy1-15,51 hidden roots,DAG,persistent boundaries and malformed controls\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

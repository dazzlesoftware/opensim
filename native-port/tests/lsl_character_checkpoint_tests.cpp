// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
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
    for (unsigned version = 1; version <= 13; ++version) {
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
        const auto wire = encode_lsl_checkpoint(c);
        check(wire[3] == version, "Character-free version changed");
        const auto d = decode_lsl_checkpoint(wire);
        check(d && encode_lsl_checkpoint(*d) == wire, "Legacy bytes changed on roundtrip");
    }
}

void units() {
    for (const char16_t unit : std::array<char16_t, 7>{0, 1, 0x3bb, 0xd800, 0xdc00, 0xfffe, 0xffff}) {
        auto c = base();
        c.globals = {LslCharacter{unit}};
        const auto wire = encode_lsl_checkpoint(c);
        check(wire[3] == 14, "Character did not select version14");
        const auto d = decode_lsl_checkpoint(wire);
        check(d && std::get<LslCharacter>(d->globals[0]).value == unit && encode_lsl_checkpoint(*d) == wire,
              "UTF16 unit roundtrip differs");
        check(!d->global_initialization_pending, "Minimal CP manufactured constructor flag");
        const auto tag = 4 + 4 + c.program_hash.size() + 4 + c.state.size() + 4 + c.next_state.size() + 4;
        check(wire[tag] == 11 && wire[tag + 1] == (unit & 255) && wire[tag + 2] == (unit >> 8),
              "Character tag/UTF16 byte order differs");
        for (std::size_t n = 0; n < wire.size(); ++n)
            check(!decode_lsl_checkpoint(std::span(wire).first(n)), "Truncated character CP accepted");
        for (unsigned v = 1; v <= 13; ++v) {
            auto bad = wire;
            bad[3] = v;
            check(!decode_lsl_checkpoint(bad), "Legacy version accepted char tag");
        }
        auto bad = wire;
        bad[tag] = 12;
        check(!decode_lsl_checkpoint(bad), "Unknown value tag accepted");
        bad = wire;
        bad.push_back(0);
        check(!decode_lsl_checkpoint(bad), "Trailing data accepted");
        bad = wire;
        bad[3] = 15;
        check(!decode_lsl_checkpoint(bad), "Unknown version accepted");
        bad = wire;
        bad[tag] = 6;
        bad[tag + 1] = 255;
        bad[tag + 2] = 255;
        check(!decode_lsl_checkpoint(bad), "Invalid reference accepted");
    }
    auto c = base();
    c.global_initialization_pending = false;
    c.active_event = LslEvent{"state_entry", {}, {}};
    c.pending_resume_fault = "fault";
    auto bad = encode_lsl_checkpoint(c);
    bad[3] = 14;
    check(!decode_lsl_checkpoint(bad), "Noncanonical char-free14 accepted");
}

void roots() {
    const LslValue ch = LslCharacter{0xd800};
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
        check(wire[3] == 14, "Hidden character root did not promote version14");
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
    check(d && wire[3] == 14, "Nested char decode failed");
    const auto l = std::get<std::shared_ptr<const LslList>>(d->globals[0]);
    check(std::get<std::shared_ptr<const LslList>>(l->items[0]) ==
                  std::get<std::shared_ptr<const LslList>>(l->items[1]) &&
              std::get<std::shared_ptr<const LslList>>(d->locals[0]) ==
                  std::get<std::shared_ptr<const LslList>>(l->items[0]),
          "Nested char DAG aliases changed");
}
} // namespace

int main() {
    try {
        legacy();
        units();
        roots();
        std::cout << "PASS: character schema14, legacy1-13 bytes, UTF16 units,17 hidden roots, DAG aliases and "
                     "malformed controls\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

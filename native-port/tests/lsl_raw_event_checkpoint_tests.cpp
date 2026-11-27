// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

namespace {
void check(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}

LslCheckpoint base() {
    LslCheckpoint c;
    c.program_hash = "raw";
    c.state = "default";
    c.globals = {std::int32_t{7}};
    c.initialized_globals = 1;
    return c;
}

void roundtrip() {
    const auto uuid = opensim::framework::Uuid::random();
    for (auto bits : std::array<std::uint32_t, 5>{0U, 0x80000000U, 0x7f800000U, 0x7fc12345U, 0xffc54321U}) {
        auto c = base();
        auto list = std::make_shared<const LslList>(
            LslList{{LslRawSingle{std::bit_cast<float>(bits)}, LslRawBoolean{true}, LslRawUuid{uuid}}});
        c.globals = {list, list};
        for (bool persistent : {false, true}) {
            auto wire = persistent ? encode_persistent_lsl_checkpoint(c) : encode_lsl_checkpoint(c);
            check(wire[3] == 18, "Raw carrier did not select schema18");
            auto d = decode_lsl_checkpoint(wire);
            check(bool(d), "Raw carrier decode failed");
            auto x = std::get<std::shared_ptr<const LslList>>(d->globals[0]);
            check(x == std::get<std::shared_ptr<const LslList>>(d->globals[1]), "Global DAG alias lost");
            check(std::bit_cast<std::uint32_t>(std::get<LslRawSingle>(x->items[0]).value) == bits,
                  "Single IEEE bits changed");
            check(std::get<LslRawBoolean>(x->items[1]).value, "Boolean changed");
            check(std::get<LslRawUuid>(x->items[2]).value == uuid, "UUID changed");
            check((persistent ? encode_persistent_lsl_checkpoint(*d) : encode_lsl_checkpoint(*d)) == wire,
                  "Raw wire noncanonical");
            for (std::size_t n = 0; n < wire.size(); ++n)
                check(!decode_lsl_checkpoint(std::span(wire).first(n)), "Truncation accepted");
            for (unsigned version = 1; version < 18; ++version) {
                auto bad = wire;
                bad[3] = version;
                check(!decode_lsl_checkpoint(bad), "Old schema accepted raw carriers");
            }
            auto bad = wire;
            bad.push_back(0);
            check(!decode_lsl_checkpoint(bad), "Trailing bytes accepted");
        }
    }
    // The first global's tag follows the three length-prefixed identity strings and count.
    const std::size_t tag = 4 + 4 + 3 + 4 + 7 + 4 + 0 + 4;
    auto c = base();
    c.globals = {LslRawBoolean{true}};
    auto wire = encode_lsl_checkpoint(c);
    check(wire[tag] == 21, "Boolean wire tag changed");
    auto bad = wire;
    bad[tag + 1] = 2;
    check(!decode_lsl_checkpoint(bad), "Noncanonical Boolean accepted");
    bad = wire;
    bad[tag] = 23;
    check(!decode_lsl_checkpoint(bad), "Unknown raw tag accepted");
    c.globals = {std::int32_t{7}};
    wire = encode_lsl_checkpoint(c);
    check(wire[3] < 18, "Legacy checkpoint promoted");
    bad = wire;
    bad[3] = 18;
    check(!decode_lsl_checkpoint(bad), "Schema18 without raw carriers accepted");
}

void field_roles() {
    auto c = base();
    auto list = std::make_shared<const LslList>(
        LslList{{LslRawSingle{1.2345679f}, LslRawBoolean{true}, LslRawUuid{opensim::framework::Uuid::random()}}});
    c.globals = {list};
    c.queued_events = {{"touch_start", {list, list}, {}}};
    auto internal = decode_lsl_checkpoint(encode_lsl_checkpoint(c));
    check(bool(internal), "Internal roles decode failed");
    check(std::get<std::shared_ptr<const LslList>>(internal->globals[0]) ==
              std::get<std::shared_ptr<const LslList>>(internal->queued_events[0].arguments[0]),
          "Internal cross-root alias lost");
    auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
    check(bool(external), "Persistent roles decode failed");
    auto a = std::get<std::shared_ptr<const LslList>>(external->queued_events[0].arguments[0]);
    auto b = std::get<std::shared_ptr<const LslList>>(external->queued_events[0].arguments[1]);
    check(a != b && a != std::get<std::shared_ptr<const LslList>>(external->globals[0]),
          "XML queue did not separate aliases");
    check(std::bit_cast<std::uint32_t>(std::get<LslRawSingle>(a->items[0]).value) == 0x3f9e0652U,
          "XML Single changed exact roundtrip value");
    check(std::holds_alternative<LslRawBoolean>(a->items[1]) && std::holds_alternative<LslRawUuid>(a->items[2]),
          "XML raw identities changed");
}

void hidden_roots() {
    for (const LslValue ch : std::array<LslValue, 3>{LslRawSingle{1.25f}, LslRawBoolean{true},
                                                     LslRawUuid{opensim::framework::Uuid::random()}}) {
        for (unsigned root = 0; root < 22; ++root) {
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
            case 17:
                e.active_event.arguments = {std::make_shared<const LslList>(LslList{{ch}})};
                c.event_contexts = {e};
                break;
            case 18: {
                LslResumeFrameCheckpoint r;
                r.method = "xmrEventDequeue";
                r.complete_restore = false;
                r.event_wait = LslEventWaitCheckpoint{9, 0, 0, 0, 1, 0};
                r.background_event = LslEvent{"touch_end", {ch}, {}};
                c.resume_frames = {r};
                break;
            }
            case 19:
                f.evaluations = {{1, ch}};
                e.call_stack = {f};
                c.event_contexts = {e};
                break;
            case 20:
                f.capture_point = p;
                e.call_stack = {f};
                c.event_contexts = {e};
                break;
            case 21:
                e.kind = LslEventContextKind::DirectHandler;
                e.event_wait.reset();
                e.direct_call_expression = 1;
                e.locals = {ch};
                c.event_contexts = {e};
                break;
            }
            for (bool persistent : {false, true}) {
                const auto wire = (persistent ? encode_persistent_lsl_checkpoint(c) : encode_lsl_checkpoint(c));
                check(wire[3] == 18, "Hidden raw CLR root did not promote version18");
                const auto d = decode_lsl_checkpoint(wire);
                check(d && (persistent ? encode_persistent_lsl_checkpoint(*d) : encode_lsl_checkpoint(*d)) == wire,
                      "Hidden raw CLR List root roundtrip differs");
            }
        }
    }
}
} // namespace

int main() {
    try {
        roundtrip();
        hidden_roots();
        field_roles();
        std::cout << "PASS schema18 raw3: IEEE bits,22 hidden roots, DAG aliases, XML roles, malformed wire\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

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
    c.program_hash = "nullable-list";
    c.state = "default";
    c.globals = {std::int32_t{7}};
    c.initialized_globals = 1;
    return c;
}

void roundtrip(bool persistent) {
    auto c = base();
    auto empty = std::make_shared<const LslList>();
    auto nested = std::make_shared<const LslList>(LslList{{LslNullObject{}, empty}});
    c.globals = {LslNullList{}, empty, nested, nested, LslNullString{}};
    c.locals = {LslNullObject{}};
    c.evaluations = {{1, LslNullList{}}};
    const auto bytes = persistent ? encode_persistent_lsl_checkpoint(c) : encode_lsl_checkpoint(c);
    check(bytes[3] == 17, "New null references did not select schema17");
    const auto decoded = decode_lsl_checkpoint(bytes);
    check(decoded.has_value(), "Nullable references did not decode");
    check(std::holds_alternative<LslNullList>(decoded->globals[0]), "Typed List null lost");
    check(std::get<std::shared_ptr<const LslList>>(decoded->globals[1])->items.empty(), "Empty List changed");
    auto a = std::get<std::shared_ptr<const LslList>>(decoded->globals[2]);
    auto b = std::get<std::shared_ptr<const LslList>>(decoded->globals[3]);
    check(a == b, "Shared List alias lost");
    check(std::holds_alternative<LslNullObject>(a->items[0]), "Raw object null changed");
    check(std::holds_alternative<LslNullObject>(decoded->locals[0]), "Local object null lost");
    check(std::holds_alternative<LslNullList>(decoded->evaluations[0].value), "Journal List null lost");
    check((persistent ? encode_persistent_lsl_checkpoint(*decoded) : encode_lsl_checkpoint(*decoded)) == bytes,
          "Nullable checkpoint is not canonical");
    for (std::size_t n = 0; n < bytes.size(); ++n)
        check(!decode_lsl_checkpoint(std::span(bytes).first(n)), "Truncation accepted");
    auto old = bytes;
    old[3] = 16;
    check(!decode_lsl_checkpoint(old), "Legacy schema accepted new null tags");
}

void hidden_roots() {
    for (const LslValue ch : std::array<LslValue, 2>{LslNullList{}, LslNullObject{}}) {
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
                if (persistent && root == 2) {
                    // Original AppendXML calls ToString on each queued argument;
                    // a raw null List/object therefore fails before queue reload.
                    bool rejected = false;
                    try {
                        (void)encode_persistent_lsl_checkpoint(c);
                    } catch (const std::runtime_error &error) {
                        rejected = std::string(error.what()) ==
                                   "NullReferenceException: Object reference not set to an instance of an object.";
                    }
                    check(rejected, "Queued raw null did not retain original XML NullReferenceException");
                    continue;
                }
                const auto wire = (persistent ? encode_persistent_lsl_checkpoint(c) : encode_lsl_checkpoint(c));
                check(wire[3] == 17, "Hidden nullable root did not promote version17");
                const auto d = decode_lsl_checkpoint(wire);
                check(d && (persistent ? encode_persistent_lsl_checkpoint(*d) : encode_lsl_checkpoint(*d)) == wire,
                      "Hidden nullable List root roundtrip differs");
            }
        }
    }
}
} // namespace

int main() {
    try {
        roundtrip(false);
        roundtrip(true);
        hidden_roots();
        auto old = base();
        old.globals = {LslNullString{}};
        auto wire = encode_lsl_checkpoint(old);
        check(wire[3] == 16, "String-only legacy schema changed");
        wire[3] = 17;
        check(!decode_lsl_checkpoint(wire), "Schema17 without new carriers accepted");
        std::cout << "Nullable List checkpoint tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

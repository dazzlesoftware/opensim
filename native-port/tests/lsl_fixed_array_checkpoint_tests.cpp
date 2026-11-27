// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_fixed_array.hpp"
#include "opensim/script/lsl_reference_string.hpp"
#include <bit>
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace opensim::script;

namespace {
using Array = std::shared_ptr<const LslFixedArray>;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

Array array(const LslValue &value) {
    return std::get<Array>(value);
}

LslCheckpoint base() {
    LslCheckpoint c;
    c.program_hash = "fixed-array-codec";
    c.state = "default";
    return c;
}

void graph() {
    auto a = array(make_lsl_fixed_array(LslFixedArrayKind::Object, 3));
    auto ints = array(make_lsl_fixed_array(LslFixedArrayKind::Integer, 2));
    lsl_fixed_array_set(ints, 0, std::int32_t{-2147483647});
    lsl_fixed_array_set(ints, 1, std::int32_t{2147483647});
    a->identity_hash = 12345;
    lsl_fixed_array_set(a, 0, a);
    lsl_fixed_array_set(a, 1, ints);
    auto list = std::make_shared<LslList>();
    list->items = {a, ints};
    lsl_fixed_array_set(a, 2, std::shared_ptr<const LslList>(list));
    auto c = base();
    c.globals = {a, ints, std::shared_ptr<const LslList>(list), a};
    c.initialized_globals = 4;
    auto wire = encode_lsl_checkpoint(c);
    check(wire[3] == 20, "Array did not select schema20");
    auto restored = decode_lsl_checkpoint(wire);
    check(bool(restored), "Cyclic graph rejected");
    auto b = array(restored->globals[0]);
    check(b == array(restored->globals[3]) && array(lsl_fixed_array_get(b, 0)) == b, "Self/outer alias lost");
    check(b->identity_hash == 12345, "Internal array identity hash lost");
    auto decoded_list = std::get<std::shared_ptr<const LslList>>(restored->globals[2]);
    check(std::get<std::shared_ptr<const LslList>>(lsl_fixed_array_get(b, 2)) == decoded_list &&
              array(decoded_list->items[0]) == b,
          "Array/List mutual cycle lost");
    check(encode_lsl_checkpoint(*restored) == wire, "Internal graph encoding unstable");
    auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
    check(external && !array(external->globals[0])->identity_hash, "External identity retained");
    check(array(lsl_fixed_array_get(array(external->globals[0]), 0)) == array(external->globals[0]),
          "External cycle lost");
    check(a->identity_hash == 12345, "Persistent writer mutated input");
    for (std::size_t n = 0; n < wire.size(); ++n)
        check(!decode_lsl_checkpoint(std::span(wire).first(n)), "Truncated cyclic graph accepted");
    auto old = wire;
    old[3] = 19;
    check(!decode_lsl_checkpoint(old), "Old schema accepted Array tag");
}

void primitives() {
    auto c = base();
    auto chars = array(make_lsl_fixed_array(LslFixedArrayKind::Character, 2));
    lsl_fixed_array_set(chars, 0, LslCharacter{0xd800});
    lsl_fixed_array_set(chars, 1, LslCharacter{0xffff});
    auto floats = array(make_lsl_fixed_array(LslFixedArrayKind::Float, 2));
    const auto nan = std::bit_cast<double>(std::uint64_t{0x7ff8000000001234});
    lsl_fixed_array_set(floats, 0, nan);
    lsl_fixed_array_set(floats, 1, -0.);
    c.globals = {chars, floats};
    auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(c));
    check(decoded && std::get<LslCharacter>(lsl_fixed_array_get(array(decoded->globals[0]), 0)).value == 0xd800,
          "Char unit changed internally");
    check(std::bit_cast<std::uint64_t>(std::get<double>(lsl_fixed_array_get(array(decoded->globals[1]), 0))) ==
              0x7ff8000000001234,
          "NaN payload changed");
    bool rejected = false;
    try {
        (void)encode_persistent_lsl_checkpoint(c);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected, "Persistent surrogate Char array accepted");
    c.globals = {make_lsl_fixed_array(LslFixedArrayKind::Object, 0)};
    const auto empty_wire = encode_persistent_lsl_checkpoint(c);
    check(empty_wire[3] == 17, "Bare external empty array selected an unrelated raw-payload profile");
    decoded = decode_lsl_checkpoint(empty_wire);
    check(decoded && std::holds_alternative<LslNullObject>(decoded->globals[0]),
          "External empty array did not become null");
    auto internal = decode_lsl_checkpoint(encode_lsl_checkpoint(c));
    check(internal && lsl_fixed_array_size(array(internal->globals[0])) == 0, "Internal empty array lost");
}

void hidden_roots() {
    for (const LslValue ch : std::array<LslValue, 2>{make_lsl_fixed_array(LslFixedArrayKind::Integer, 1),
                                                     make_lsl_fixed_array(LslFixedArrayKind::Object, 1)}) {
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
                    const auto wire = encode_persistent_lsl_checkpoint(c);
                    check(wire[3] == 20 && !decode_lsl_checkpoint(wire),
                          "Queued XML array did not write successfully then fail reload");
                    continue;
                }
                const auto wire = (persistent ? encode_persistent_lsl_checkpoint(c) : encode_lsl_checkpoint(c));
                check(wire[3] == 20, "Hidden nullable root did not promote version20");
                const auto d = decode_lsl_checkpoint(wire);
                check(d && (persistent ? encode_persistent_lsl_checkpoint(*d) : encode_lsl_checkpoint(*d)) == wire,
                      "Hidden nullable List root roundtrip differs");
            }
        }
    }
}

void collection() {
    std::weak_ptr<const LslFixedArray> weak;
    {
        auto a = array(make_lsl_fixed_array(LslFixedArrayKind::Object, 1));
        weak = a;
        lsl_fixed_array_set(a, 0, a);
        collect_lsl_fixed_arrays();
        check(!weak.expired() && lsl_fixed_array_size(a) == 1, "Collector cleared externally rooted cycle");
    }
    collect_lsl_fixed_arrays();
    check(weak.expired(), "Unreachable self cycle leaked");
    std::weak_ptr<const LslList> weak_list;
    {
        auto a = array(make_lsl_fixed_array(LslFixedArrayKind::Object, 1));
        weak = a;
        auto list = std::make_shared<LslList>();
        list->items = {a};
        weak_list = list;
        lsl_fixed_array_set(a, 0, std::shared_ptr<const LslList>(list));
    }
    collect_lsl_fixed_arrays();
    check(weak.expired() && weak_list.expired(), "Unreachable Array/List cycle leaked");
    std::shared_ptr<const LslList> external_list;
    {
        auto a = array(make_lsl_fixed_array(LslFixedArrayKind::Object, 1));
        weak = a;
        external_list = std::make_shared<const LslList>(LslList{{a}});
        lsl_fixed_array_set(a, 0, external_list);
    }
    collect_lsl_fixed_arrays();
    check(!weak.expired(), "Collector ignored external List root");
    external_list.reset();
    collect_lsl_fixed_arrays();
    check(weak.expired(), "Dropped external List root retained cycle");
}

void malformed() {
    auto c = base();
    c.globals = {make_lsl_fixed_array(LslFixedArrayKind::Integer, 2)};
    const auto wire = encode_lsl_checkpoint(c);
    const std::array<std::uint8_t, 8> pattern{25, 0, 2, 0, 2, 0, 0, 0};
    const auto found = std::search(wire.begin(), wire.end(), pattern.begin(), pattern.end());
    check(found != wire.end(), "Array wire pattern absent");
    const auto offset = static_cast<std::size_t>(found - wire.begin());
    auto bad = wire;
    bad[offset + 1] = 3;
    check(!decode_lsl_checkpoint(bad), "Unknown array marker accepted");
    bad = wire;
    bad[offset + 2] = 4;
    check(!decode_lsl_checkpoint(bad), "Unknown array kind accepted");
    bad = wire;
    bad[offset + 1] = 1;
    check(!decode_lsl_checkpoint(bad), "Forward array reference accepted");
    bad = wire;
    bad[offset + 3] = 1;
    bad.insert(bad.begin() + offset + 4, 4, 0);
    check(!decode_lsl_checkpoint(bad), "Zero array identity hash accepted");
}

void type_name_origin() {
    for (std::uint8_t kind = 0; kind < 4; ++kind) {
        const auto name = make_lsl_cached_array_type_string(kind);
        const auto ptr = std::get<std::shared_ptr<const LslStringReference>>(name);
        check(!lsl_reference_string_is_literal(ptr) && lsl_reference_string_cached_array_type(ptr) == kind,
              "Runtime type text lost nonliteral origin");
        auto c = base();
        c.globals = {name};
        const auto wire = encode_lsl_checkpoint(c);
        check(wire[3] == 20, "Type cache origin alone did not select20");
        auto internal = decode_lsl_checkpoint(wire);
        check(internal && std::get<std::shared_ptr<const LslStringReference>>(internal->globals[0]) == ptr,
              "Internal RuntimeType cache identity lost");
        auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
        check(external && std::get<std::shared_ptr<const LslStringReference>>(external->globals[0]) != ptr,
              "External RuntimeType cache origin retained");
    }
}

void ledgers() {
    auto c = base();
    c.active_event = LslEvent{"state_entry", {}, {}};
    c.global_initialization_pending = false;
    c.locals = {std::int32_t{7}};
    c.arrays_heap_usage = -108;
    c.locals_heap_usage = 20;
    c.local_heap_base = 28;
    c.local_object_heap_usage = {8};
    c.heap_restore_pending = true;
    LslCapturePointCheckpoint p;
    p.local_heap_base = -1;
    p.local_object_heap_usage = {std::nullopt, 12};
    c.capture_point = p;
    LslCallFrameCheckpoint f;
    f.function = "worker";
    f.locals = {std::int32_t{1}};
    f.local_heap_base = 32;
    f.local_object_heap_usage = {16};
    f.capture_point = p;
    c.call_stack = {f};
    LslEventContextCheckpoint e;
    e.active_event = {"touch_start", {}, {}};
    e.event_wait = LslEventWaitCheckpoint{9, 0, 0, 0, 1, 0};
    e.locals = {std::int32_t{2}};
    e.local_heap_base = 40;
    e.local_object_heap_usage = {24};
    e.call_stack = {f};
    e.capture_point = p;
    c.event_contexts = {e};
    LslResumeFrameCheckpoint r;
    r.method = "worker";
    r.complete_restore = true;
    r.locals = {std::int32_t{3}};
    r.local_heap_base = 48;
    r.local_object_heap_usage = {32};
    c.resume_frames = {r};
    auto wire = encode_lsl_checkpoint(c);
    check(wire[3] == 20, "Ledger-only state did not select20");
    auto d = decode_lsl_checkpoint(wire);
    check(bool(d), "Ledger checkpoint rejected");
    check(d->arrays_heap_usage == -108 && d->locals_heap_usage == 20 && d->local_heap_base == 28 &&
              d->heap_restore_pending,
          "Signed root ledger changed");
    check(d->local_object_heap_usage == c.local_object_heap_usage &&
              d->capture_point->local_object_heap_usage == p.local_object_heap_usage &&
              d->call_stack[0].local_heap_base == 32 && d->event_contexts[0].local_heap_base == 40 &&
              d->resume_frames[0].local_heap_base == 48,
          "Nested owner ledger changed");
    check(encode_lsl_checkpoint(*d) == wire, "Ledger canonical roundtrip changed");
    auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
    check(external && !external->arrays_heap_usage && !external->locals_heap_usage && !external->local_heap_base &&
              !external->heap_restore_pending && external->local_object_heap_usage.empty() &&
              !external->call_stack[0].local_heap_base && !external->event_contexts[0].local_heap_base &&
              !external->resume_frames[0].local_heap_base && !external->capture_point->local_heap_base,
          "External migration retained internal heap ledger");
    c.local_object_heap_usage = {1, 2};
    check(!decode_lsl_checkpoint(encode_lsl_checkpoint(c)), "Mismatched local ledger arity accepted");
}
} // namespace

int main() {
    try {
        graph();
        primitives();
        hidden_roots();
        collection();
        malformed();
        type_name_origin();
        ledgers();
        collect_lsl_fixed_arrays();
        std::cout << "Fixed-array checkpoint tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

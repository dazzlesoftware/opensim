// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace opensim;
using namespace script;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

LslCheckpoint base() {
    LslCheckpoint checkpoint;
    checkpoint.program_hash = "original-source";
    checkpoint.state = "default";
    checkpoint.globals = {std::int32_t{1}};
    checkpoint.initialized_globals = 1;
    return checkpoint;
}

void legacy_versions() {
    for (int version = 1; version <= 8; ++version) {
        auto checkpoint = base();
        if (version == 2 || version == 6) {
            LslListenerCheckpoint listener{};
            listener.handle = 1;
            listener.regex_flags = version == 6 ? 1 : 0;
            checkpoint.listeners.push_back(listener);
        }
        if (version == 3)
            checkpoint.permissions_mask = 1;
        if (version == 4)
            checkpoint.start_parameter = 1;
        if (version == 5) {
            LslSensorQuery sensor{};
            sensor.interval = 1.;
            checkpoint.sensors.push_back(sensor);
        }
        if (version == 7) {
            auto shared = std::make_shared<LslList>();
            shared->items = {std::int32_t{7}};
            checkpoint.globals = {std::shared_ptr<const LslList>(shared), std::shared_ptr<const LslList>(shared)};
        }
        if (version == 8)
            checkpoint.function = "worker:0,";
        const auto bytes = encode_lsl_checkpoint(checkpoint);
        check(bytes[3] == version, "Wait-free checkpoint changed its original version selection");
        const auto restored = decode_lsl_checkpoint(bytes);
        check(restored && !restored->event_wait && encode_lsl_checkpoint(*restored) == bytes,
              "Legacy checkpoint no longer round-trips byte-for-byte");
        checkpoint.event_wait = LslEventWaitCheckpoint{0, 0, 0, 0};
        const auto waited = encode_lsl_checkpoint(checkpoint);
        check(waited[3] == 9, "Active event wait did not select version9");
        auto resumed = decode_lsl_checkpoint(waited);
        check(resumed && resumed->event_wait && encode_lsl_checkpoint(*resumed) == waited,
              "Wait extension failed on a legacy checkpoint layout");
        resumed->event_wait.reset();
        check(encode_lsl_checkpoint(*resumed) == bytes, "Removing the wait did not restore exact legacy bytes");
    }
}

void store_u64(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
}

void typed_roundtrip_and_rejection() {
    auto checkpoint = base();
    auto shared = std::make_shared<LslList>();
    shared->items = {std::int32_t{7}, std::string("typed"), 1.5, framework::Vector3d{1, 2, 3}, LslRotation{0, 0, 0, 1}};
    const auto immutable = std::shared_ptr<const LslList>(shared);
    checkpoint.globals = {immutable};
    checkpoint.locals = {immutable};
    checkpoint.function = "inner:0,";
    checkpoint.evaluations.push_back({6, immutable});
    LslCallFrameCheckpoint caller;
    caller.function = "outer:0,";
    caller.instruction = 3;
    caller.return_expression = 8;
    caller.locals = {immutable};
    caller.evaluations.push_back({7, immutable});
    checkpoint.call_stack.push_back(caller);
    LslDetector detector;
    detector.key = framework::Uuid::random();
    detector.name = "original detector";
    detector.touch_face = 5;
    detector.position = {10, 20, 30};
    detector.link_number = 3;
    checkpoint.active_event = LslEvent{"touch_start", {std::int32_t{1}}, {detector}};
    checkpoint.queued_events.push_back({"changed", {std::int32_t{256}, immutable}, {detector}});
    checkpoint.event_wait =
        LslEventWaitCheckpoint{9, 3155378975999999999LL, std::numeric_limits<std::int32_t>::min(), -1};
    const auto bytes = encode_lsl_checkpoint(checkpoint);
    auto decoded = decode_lsl_checkpoint(bytes);
    check(decoded && decoded->event_wait && decoded->event_wait->expression == 9 &&
              decoded->event_wait->deadline_utc_ticks == 3155378975999999999LL &&
              decoded->event_wait->return_mask1 == std::numeric_limits<std::int32_t>::min() &&
              decoded->event_wait->return_mask2 == -1 && encode_lsl_checkpoint(*decoded) == bytes,
          "Typed version9 wait fields changed or lost signed mask bits");
    const auto list = std::get<std::shared_ptr<const LslList>>(decoded->globals.front());
    check(list == std::get<std::shared_ptr<const LslList>>(decoded->locals.front()) &&
              list == std::get<std::shared_ptr<const LslList>>(decoded->evaluations.front().value) &&
              list == std::get<std::shared_ptr<const LslList>>(decoded->call_stack.front().locals.front()) &&
              list == std::get<std::shared_ptr<const LslList>>(decoded->call_stack.front().evaluations.front().value) &&
              list == std::get<std::shared_ptr<const LslList>>(decoded->queued_events.front().arguments.back()),
          "Wait extension detached typed list aliases across frames/journals/events");
    check(decoded->active_event && decoded->active_event->name == "touch_start" &&
              decoded->active_event->detectors.size() == 1 &&
              decoded->active_event->detectors.front().key == detector.key &&
              decoded->active_event->detectors.front().position == detector.position &&
              decoded->queued_events.front().detectors.front().touch_face == 5,
          "Wait extension overwrote outer active event or detectors");
    const auto core = decode_lsl_checkpoint(encode_lsl_checkpoint(checkpoint, false));
    check(core && core->event_wait && core->call_stack.size() == 1 && core->evaluations.size() == 1,
          "Excluding service extensions removed core wait/callframe state");
    for (std::size_t length = 0; length < bytes.size(); ++length)
        check(!decode_lsl_checkpoint(std::span(bytes).first(length)), "Truncated version9 wait was accepted");
    auto invalid = bytes;
    invalid.push_back(0);
    check(!decode_lsl_checkpoint(invalid), "Trailing wait payload was accepted");
    for (const auto version : {0, 14, 255}) {
        invalid = bytes;
        invalid[3] = static_cast<std::uint8_t>(version);
        check(!decode_lsl_checkpoint(invalid), "Unsupported checkpoint version was accepted");
    }
    constexpr std::size_t suffix_size = 25;
    const auto suffix = bytes.size() - suffix_size;
    invalid = bytes;
    invalid[suffix] = 2;
    check(!decode_lsl_checkpoint(invalid), "Malformed wait presence flag was accepted");
    invalid = bytes;
    invalid.resize(suffix + 1);
    invalid[suffix] = 0;
    check(!decode_lsl_checkpoint(invalid), "Version9 without an active wait was accepted");
    for (const auto deadline : {std::uint64_t{3155378976000000000ULL}, std::uint64_t(-1)}) {
        invalid = bytes;
        store_u64(invalid, suffix + 9, deadline);
        check(!decode_lsl_checkpoint(invalid), "Invalid absolute wait deadline was accepted");
    }
    invalid = bytes;
    store_u64(invalid, suffix + 1, 6);
    check(!decode_lsl_checkpoint(invalid), "Wait expression already completed in current journal was accepted");
    for (const bool bad_deadline : {false, true}) {
        auto malformed = checkpoint;
        if (bad_deadline)
            malformed.event_wait->deadline_utc_ticks = -1;
        else
            malformed.event_wait->expression = 6;
        bool rejected = false;
        try {
            encode_lsl_checkpoint(malformed);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Invalid native wait was encoded");
    }
    auto oversized = checkpoint;
    oversized.globals.push_back(std::string(4 * 1024 * 1024, 'x'));
    const auto large_wire = encode_lsl_checkpoint(oversized);
    const auto large_restored = decode_lsl_checkpoint(large_wire);
    check(large_wire.size() > 4 * 1024 * 1024 && large_restored &&
              std::get<std::string>(large_restored->globals.back()) == std::get<std::string>(oversized.globals.back()),
          "Large wait checkpoint failed roundtrip");
}

void nested_contexts() {
    auto checkpoint = base();
    auto shared = std::make_shared<LslList>();
    shared->items = {std::string("cross-context alias"), std::int32_t{91}};
    const auto immutable = std::shared_ptr<const LslList>(shared);
    checkpoint.globals = {immutable};
    checkpoint.locals = {immutable};
    checkpoint.function = "background:0,";
    checkpoint.evaluations.push_back({100, immutable});
    LslDetector outer_detector, middle_detector, current_detector;
    outer_detector.key = framework::Uuid::random();
    middle_detector.key = framework::Uuid::random();
    current_detector.key = framework::Uuid::random();
    checkpoint.active_event = LslEvent{"listen", {immutable}, {current_detector}};
    LslEventContextCheckpoint outer;
    outer.active_event = {"state_entry", {immutable}, {outer_detector}};
    outer.function = "outer:0,";
    outer.instruction = 4;
    outer.locals = {immutable};
    outer.evaluations.push_back({501, immutable});
    outer.event_wait = LslEventWaitCheckpoint{600, 3155378975999999999LL, 1 << 3, 0, 1 << 2, 0};
    LslCallFrameCheckpoint caller;
    caller.function = "caller:0,";
    caller.locals = {immutable};
    caller.evaluations.push_back({502, immutable});
    outer.call_stack.push_back(caller);
    auto middle = outer;
    middle.active_event = {"timer", {immutable}, {middle_detector}};
    middle.function = "middle:0,";
    middle.event_wait = LslEventWaitCheckpoint{800, 3155378975999999999LL, 1 << 3, 0, 1 << 15, 0};
    checkpoint.event_contexts = {outer, middle};
    checkpoint.event_wait = LslEventWaitCheckpoint{900, 3155378975999999999LL, 0, 2, 1 << 2, 0};
    const auto bytes = encode_lsl_checkpoint(checkpoint);
    check(bytes[3] == 10, "Nested background contexts did not select version10");
    const auto decoded = decode_lsl_checkpoint(bytes);
    check(decoded && decoded->event_contexts.size() == 2 && decoded->event_wait &&
              encode_lsl_checkpoint(*decoded) == bytes,
          "Nested background context did not round-trip exactly");
    const auto list = std::get<std::shared_ptr<const LslList>>(decoded->globals.front());
    for (std::size_t i = 0; i < decoded->event_contexts.size(); ++i) {
        const auto &context = decoded->event_contexts[i];
        check(list == std::get<std::shared_ptr<const LslList>>(context.locals.front()) &&
                  list == std::get<std::shared_ptr<const LslList>>(context.evaluations.front().value) &&
                  list == std::get<std::shared_ptr<const LslList>>(context.active_event.arguments.front()) &&
                  list == std::get<std::shared_ptr<const LslList>>(context.call_stack.front().locals.front()) &&
                  list ==
                      std::get<std::shared_ptr<const LslList>>(context.call_stack.front().evaluations.front().value),
              "Background serialization detached aliases across outer event frames");
    }
    check(decoded->active_event->detectors.front().key == current_detector.key &&
              decoded->event_contexts[0].active_event.detectors.front().key == outer_detector.key &&
              decoded->event_contexts[1].active_event.detectors.front().key == middle_detector.key &&
              decoded->event_contexts[0].event_wait->expression == 600 &&
              decoded->event_contexts[1].event_wait->expression == 800 &&
              decoded->event_contexts[1].event_wait->background_mask1 == (1 << 15),
          "Background context order, masks, wait ownership or detector identity changed");
    const auto core = decode_lsl_checkpoint(encode_lsl_checkpoint(checkpoint, false));
    check(core && core->event_contexts.size() == 2 && core->event_wait,
          "Service extension exclusion discarded core background event contexts");
    checkpoint.event_wait.reset();
    check(decode_lsl_checkpoint(encode_lsl_checkpoint(checkpoint))->event_contexts.size() == 2,
          "Executing background handler without a current wait lost its outer contexts");
    checkpoint.event_contexts.clear();
    checkpoint.event_wait = LslEventWaitCheckpoint{900, 100000, 1 << 3, 0};
    const auto version9 = encode_lsl_checkpoint(checkpoint);
    check(version9[3] == 9, "Ordinary wait stopped using exact version9 layout");
    checkpoint.event_wait->background_mask1 = 1 << 2;
    check(encode_lsl_checkpoint(checkpoint)[3] == 10,
          "Waiting background masks without an active background frame did not select version10");
    checkpoint.event_wait->background_mask1 = 0;
    check(encode_lsl_checkpoint(checkpoint) == version9,
          "Removing exclusive background masks changed old version9 bytes");
    for (std::size_t length = 0; length < bytes.size(); ++length)
        check(!decode_lsl_checkpoint(std::span(bytes).first(length)), "Truncated nested event context was accepted");
    auto invalid = bytes;
    invalid.push_back(0);
    check(!decode_lsl_checkpoint(invalid), "Trailing nested context payload was accepted");
    invalid = bytes;
    store_u64(invalid, invalid.size() - 32 + 8, std::uint64_t(-1));
    check(!decode_lsl_checkpoint(invalid), "Malformed outer wait deadline was accepted");
    invalid = bytes;
    store_u64(invalid, invalid.size() - 32, 501);
    check(!decode_lsl_checkpoint(invalid), "Outer wait already completed in its journal was accepted");
    for (int scenario = 0; scenario < 4; ++scenario) {
        auto malformed = *decoded;
        if (scenario == 0)
            malformed.active_event.reset();
        else if (scenario == 1)
            malformed.event_contexts.front().active_event.name.clear();
        else if (scenario == 2)
            malformed.event_contexts.front().event_wait->background_mask1 = 0;
        else
            malformed.event_contexts.front().event_wait->background_mask1 = 1 << 3;
        bool rejected = false;
        try {
            encode_lsl_checkpoint(malformed);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Malformed outer event context ownership or noncanonical masks were encoded");
    }
    auto many = base();
    many.active_event = LslEvent{"timer", {}, {}};
    LslEventContextCheckpoint context;
    context.active_event = {"state_entry", {}, {}};
    context.event_wait = LslEventWaitCheckpoint{0, 100000, 0, 0, 1 << 2, 0};
    many.event_contexts.resize(65, context);
    const auto deep_contexts = decode_lsl_checkpoint(encode_lsl_checkpoint(many));
    check(deep_contexts && deep_contexts->event_contexts.size() == 65,
          "Flat event context stack acquired an invented 64-context limit");
    many.event_contexts.resize(65537, context);
    const auto large_contexts = decode_lsl_checkpoint(encode_lsl_checkpoint(many));
    check(large_contexts && large_contexts->event_contexts.size() == 65537,
          "Representable event context collection failed codec roundtrip");
    std::shared_ptr<const LslList> nested = immutable;
    for (int depth = 0; depth < 65; ++depth) {
        auto container = std::make_shared<LslList>();
        container->items.emplace_back(nested);
        nested = std::move(container);
    }
    auto recursive = *decoded;
    recursive.event_contexts.front().locals = {nested};
    bool bounded = false;
    try {
        encode_lsl_checkpoint(recursive);
    } catch (const std::invalid_argument &) {
        bounded = true;
    }
    check(bounded, "Nested context values bypassed existing recursive list depth bounds");
}

void boxed_values() {
    auto checkpoint = base();
    auto list = std::make_shared<LslList>();
    list->items = {LslBoxedInteger{0x12345678}, LslBoxedFloat{-0.0},
                   LslBoxedString{"text"},      LslBoxedKey{std::string("key\0suffix", 10)},
                   std::int32_t{0x12345678},    2.5,
                   std::string("text")};
    const std::shared_ptr<const LslList> shared = list;
    checkpoint.globals = {shared, shared};
    checkpoint.locals = {shared};
    checkpoint.active_event = LslEvent{"timer", {shared}, {}};
    checkpoint.call_stack.push_back({"", 1, 3, {shared}, {{4, shared}}});
    checkpoint.evaluations.push_back({5, shared});
    LslEventContextCheckpoint outer;
    outer.active_event = {"state_entry", {shared}, {}};
    outer.event_wait = LslEventWaitCheckpoint{9, 100000, 0, 0, 1 << 2, 0};
    outer.locals = {shared};
    outer.evaluations = {{8, shared}};
    outer.call_stack = {{"", 2, 7, {shared}, {{6, shared}}}};
    checkpoint.event_contexts.push_back(outer);
    const auto bytes = encode_lsl_checkpoint(checkpoint);
    check(bytes[3] == 11, "Boxed list payload did not select checkpoint11");
    const auto decoded = decode_lsl_checkpoint(bytes);
    check(decoded && encode_lsl_checkpoint(*decoded) == bytes, "Boxed values changed during wire roundtrip");
    const auto restored = std::get<std::shared_ptr<const LslList>>(decoded->globals[0]);
    check(std::get<LslBoxedInteger>(restored->items[0]).value == 0x12345678 &&
              std::bit_cast<std::uint64_t>(std::get<LslBoxedFloat>(restored->items[1]).value) ==
                  std::bit_cast<std::uint64_t>(-0.0) &&
              std::get<LslBoxedString>(restored->items[2]).value == "text" &&
              std::get<LslBoxedKey>(restored->items[3]).value == std::string("key\0suffix", 10) &&
              std::holds_alternative<std::int32_t>(restored->items[4]) &&
              std::holds_alternative<double>(restored->items[5]) &&
              std::holds_alternative<std::string>(restored->items[6]),
          "Boxing identity, raw CLR identity, exact float bits or embedded text was erased");
    check(restored == std::get<std::shared_ptr<const LslList>>(decoded->globals[1]) &&
              restored == std::get<std::shared_ptr<const LslList>>(decoded->locals[0]) &&
              restored == std::get<std::shared_ptr<const LslList>>(decoded->active_event->arguments[0]) &&
              restored == std::get<std::shared_ptr<const LslList>>(decoded->call_stack[0].locals[0]) &&
              restored == std::get<std::shared_ptr<const LslList>>(decoded->evaluations[0].value) &&
              restored == std::get<std::shared_ptr<const LslList>>(decoded->event_contexts[0].locals[0]) &&
              restored ==
                  std::get<std::shared_ptr<const LslList>>(decoded->event_contexts[0].active_event.arguments[0]) &&
              restored == std::get<std::shared_ptr<const LslList>>(decoded->event_contexts[0].call_stack[0].locals[0]),
          "Boxed values detached list aliases across current and outer event frames");
    check(encode_lsl_checkpoint(checkpoint, false)[3] == 11,
          "Excluding service extensions erased core boxed type information");
    for (std::size_t length = 0; length < bytes.size(); ++length)
        check(!decode_lsl_checkpoint(std::span(bytes).first(length)), "Truncated boxed payload was accepted");
    auto invalid = bytes;
    invalid[3] = 10;
    check(!decode_lsl_checkpoint(invalid), "Legacy checkpoint accepted new boxed value tags");
    invalid = bytes;
    constexpr std::array<std::uint8_t, 5> boxed_marker{7, 0x78, 0x56, 0x34, 0x12};
    const auto tag = std::search(invalid.begin(), invalid.end(), boxed_marker.begin(), boxed_marker.end());
    check(tag != invalid.end(), "New integer wire tag collided with old list-reference tag");
    *tag = 11;
    check(!decode_lsl_checkpoint(invalid), "Unknown boxed value tag was accepted");
    auto plain = base();
    const auto legacy = encode_lsl_checkpoint(plain);
    check(legacy[3] == 1, "Unboxed values stopped using their old wire version");
    // Exercise schema selection when the sole box is in a late outer journal.
    plain.active_event = LslEvent{"timer", {}, {}};
    outer.locals.clear();
    outer.active_event.arguments.clear();
    outer.call_stack.clear();
    outer.evaluations.clear();
    plain.event_contexts = {outer};
    const auto version10 = encode_lsl_checkpoint(plain);
    check(version10[3] == 10, "Ordinary background context stopped using exact version10");
    plain.event_contexts[0].evaluations.push_back({8, LslBoxedKey{"late box"}});
    const auto late = decode_lsl_checkpoint(encode_lsl_checkpoint(plain));
    check(late && std::holds_alternative<LslBoxedKey>(late->event_contexts[0].evaluations[0].value),
          "Late boxed journal value selected the wrong extension schema");
    plain.event_contexts[0].evaluations.clear();
    check(encode_lsl_checkpoint(plain) == version10, "Removing boxing changed exact old version10 bytes");
    auto noncanonical = version10;
    noncanonical[3] = 11;
    check(!decode_lsl_checkpoint(noncanonical), "Unboxed checkpoint11 payload was accepted");
}

void constructor_contexts() {
    auto saved = base();
    const auto legacy = encode_lsl_checkpoint(saved);
    saved.global_initialization_pending = true;
    const auto pending = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    check(pending && pending->global_initialization_pending == true && !pending->active_event &&
              encode_lsl_checkpoint(*pending)[3] == 12,
          "Pending constructor without an admitted event lost its explicit phase state");
    saved.active_event = LslEvent{"state_entry", {}, {}};
    saved.global_initializer_cursor = 0;
    saved.event_wait = LslEventWaitCheckpoint{7, 100000, 1 << 21, 0};
    auto values = std::make_shared<LslList>();
    values->items = {LslBoxedString{"constructor"}, LslBoxedInteger{7}};
    const std::shared_ptr<const LslList> shared = values;
    saved.globals = {shared};
    saved.evaluations = {{9, shared}};
    const auto bytes = encode_lsl_checkpoint(saved);
    const auto restored = decode_lsl_checkpoint(bytes);
    check(restored && restored->global_initialization_pending == true && restored->global_initializer_cursor == 0 &&
              restored->event_wait && bytes[3] == 12 && encode_lsl_checkpoint(*restored) == bytes &&
              std::get<std::shared_ptr<const LslList>>(restored->globals[0]) ==
                  std::get<std::shared_ptr<const LslList>>(restored->evaluations[0].value),
          "Constructor wait, boxed values, aliases or initializer cursor changed in checkpoint12");
    const auto core = decode_lsl_checkpoint(encode_lsl_checkpoint(saved, false));
    check(core && core->global_initialization_pending == true && core->global_initializer_cursor == 0,
          "Excluding service extensions removed constructor execution state");
    for (std::size_t length = 0; length < bytes.size(); ++length)
        check(!decode_lsl_checkpoint(std::span(bytes).first(length)), "Truncated constructor checkpoint was accepted");
    auto invalid = bytes;
    invalid[invalid.size() - 10] = 2;
    check(!decode_lsl_checkpoint(invalid), "Non-Boolean constructor flag was accepted");
    invalid = bytes;
    invalid[invalid.size() - 9] = 2;
    check(!decode_lsl_checkpoint(invalid), "Invalid constructor cursor presence flag was accepted");
    invalid = bytes;
    store_u64(invalid, invalid.size() - 8, saved.globals.size());
    check(!decode_lsl_checkpoint(invalid), "Constructor cursor beyond allocated globals was accepted");
    invalid = bytes;
    invalid.erase(invalid.end() - 10);
    invalid[invalid.size() - 10] = 0;
    check(!decode_lsl_checkpoint(invalid), "Checkpoint12 omitted its required scriptwide flag");
    invalid = bytes;
    invalid[3] = 11;
    check(!decode_lsl_checkpoint(invalid), "Legacy boxed schema consumed new constructor fields");

    LslEventContextCheckpoint outer;
    outer.active_event = {"state_entry", {shared}, {}};
    outer.event_wait = LslEventWaitCheckpoint{5, 100000, 0, 0, 1 << 21, 0};
    outer.global_initializer_cursor = 0;
    outer.evaluations = {{9, shared}};
    saved.active_event = LslEvent{"touch_start", {std::int32_t{1}}, {}};
    saved.global_initializer_cursor.reset();
    saved.event_wait.reset();
    saved.global_initialization_pending = false;
    saved.event_contexts = {outer};
    const auto background_bytes = encode_lsl_checkpoint(saved);
    const auto background = decode_lsl_checkpoint(background_bytes);
    check(background && background->global_initialization_pending == false &&
              background->event_contexts[0].global_initializer_cursor == 0 &&
              encode_lsl_checkpoint(*background) == background_bytes &&
              std::get<std::shared_ptr<const LslList>>(background->globals[0]) ==
                  std::get<std::shared_ptr<const LslList>>(background->event_contexts[0].evaluations[0].value),
          "Completed nested constructor flag discarded a still-suspended outer initializer or aliases");
    for (int scenario = 0; scenario < 5; ++scenario) {
        auto malformed = saved;
        if (scenario == 0)
            malformed.global_initialization_pending.reset();
        else if (scenario == 1)
            malformed.event_contexts[0].active_event.name = "timer";
        else if (scenario == 2)
            malformed.event_contexts[0].global_initializer_cursor = malformed.globals.size();
        else if (scenario == 3) {
            malformed.global_initializer_cursor = 0;
            malformed.active_event.reset();
        } else
            malformed.state = "other";
        bool rejected = false;
        try {
            encode_lsl_checkpoint(malformed);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Invalid constructor phase ownership or cursor was encoded");
    }
    auto completed = base();
    completed.global_initialization_pending = false;
    auto explicit_false = decode_lsl_checkpoint(encode_lsl_checkpoint(completed));
    check(explicit_false && explicit_false->global_initialization_pending == false &&
              encode_lsl_checkpoint(*explicit_false)[3] == 12,
          "Completed constructor flag was confused with an absent legacy flag");
    explicit_false->global_initialization_pending.reset();
    check(encode_lsl_checkpoint(*explicit_false) == legacy,
          "Removing explicit constructor state changed exact legacy checkpoint bytes");
}

void direct_handler_contexts() {
    auto saved = base();
    saved.global_initialization_pending = false;
    saved.active_event = LslEvent{"timer", {}, {}};
    const auto version12 = encode_lsl_checkpoint(saved);
    auto shared = std::make_shared<LslList>();
    shared->items = {LslBoxedString{"direct alias"}, LslBoxedInteger{7}};
    const std::shared_ptr<const LslList> immutable = shared;
    saved.globals = {immutable};
    saved.locals = {immutable};
    saved.capture_point = LslCapturePointCheckpoint{2, {{8, immutable}}, true};
    LslCallFrameCheckpoint caller{"worker", 3, 9, {immutable}, {{7, immutable}}};
    caller.capture_point = LslCapturePointCheckpoint{1, {{6, immutable}}, false};
    saved.call_stack = {caller};
    LslEventContextCheckpoint outer;
    outer.active_event = {"state_entry", {immutable}, {}};
    outer.kind = LslEventContextKind::DirectHandler;
    outer.direct_call_expression = std::size_t{0x11223344UL};
    outer.global_initializer_cursor = 0;
    outer.capture_point = LslCapturePointCheckpoint{0, {{5, immutable}}, true};
    outer.invoked_event = LslEvent{"timer", {immutable}, {}};
    outer.call_stack = {caller};
    saved.event_contexts = {outer};
    LslResumeFrameCheckpoint method;
    method.method = "default timer";
    method.locals = {immutable};
    method.evaluations = {{4, immutable}};
    method.complete_restore = true;
    LslResumeFrameCheckpoint background;
    background.method = "xmrEventDequeue";
    background.event_wait = LslEventWaitCheckpoint{12, 100000, 1 << 21, 0, 1 << 2, 0};
    background.background_event = LslEvent{"timer", {immutable}, {}};
    auto waiting = background;
    waiting.complete_restore = true;
    waiting.background_event.reset();
    saved.resume_frames = {method, background, waiting};
    saved.resume_entry_pending = true;
    const auto bytes = encode_lsl_checkpoint(saved);
    const auto decoded = decode_lsl_checkpoint(bytes);
    check(bytes[3] == 13 && decoded && encode_lsl_checkpoint(*decoded) == bytes &&
              decoded->event_contexts[0].kind == LslEventContextKind::DirectHandler &&
              !decoded->event_contexts[0].event_wait && decoded->capture_point->check_stack &&
              decoded->event_contexts[0].global_initializer_cursor == 0 && decoded->resume_entry_pending &&
              decoded->resume_frames.size() == 3 && decoded->resume_frames[1].background_event &&
              decoded->resume_frames[2].complete_restore && !decoded->resume_frames[2].background_event,
          "Direct-handler context, capture points or replay frame kinds changed in checkpoint13");
    const auto restored = std::get<std::shared_ptr<const LslList>>(decoded->globals[0]);
    check(restored == std::get<std::shared_ptr<const LslList>>(decoded->capture_point->evaluations[0].value) &&
              restored == std::get<std::shared_ptr<const LslList>>(
                              decoded->call_stack[0].capture_point->evaluations[0].value) &&
              restored ==
                  std::get<std::shared_ptr<const LslList>>(decoded->event_contexts[0].invoked_event->arguments[0]) &&
              restored == std::get<std::shared_ptr<const LslList>>(decoded->resume_frames[0].locals[0]) &&
              restored ==
                  std::get<std::shared_ptr<const LslList>>(decoded->resume_frames[1].background_event->arguments[0]),
          "Capture/replay metadata detached shared boxed list aliases");
    saved.event_contexts[0].direct_call_expression = 0;
    const auto zero = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    check(zero && zero->event_contexts[0].direct_call_expression.has_value() &&
              *zero->event_contexts[0].direct_call_expression == 0,
          "Valid expression ID zero was confused with an absent direct-call expression");
    const auto core = decode_lsl_checkpoint(encode_lsl_checkpoint(saved, false));
    check(core && core->capture_point && core->resume_frames.size() == 3 && core->resume_entry_pending &&
              core->event_contexts[0].kind == LslEventContextKind::DirectHandler,
          "Excluding service extensions removed direct-call execution state");
    for (std::size_t length = 0; length < bytes.size(); ++length)
        check(!decode_lsl_checkpoint(std::span(bytes).first(length)), "Truncated direct-call checkpoint was accepted");
    auto invalid = bytes;
    invalid[3] = 12;
    check(!decode_lsl_checkpoint(invalid), "Legacy constructor schema consumed direct-handler metadata");
    constexpr std::array<std::uint8_t, 11> marker{1, 0, 1, 0x44, 0x33, 0x22, 0x11, 0, 0, 0, 0};
    for (std::size_t position = 0; position < 3; ++position) {
        invalid = bytes;
        const auto found = std::search(invalid.begin(), invalid.end(), marker.begin(), marker.end());
        check(found != invalid.end(), "Direct-handler wire marker is missing");
        found[position] = 2;
        check(!decode_lsl_checkpoint(invalid), "Unknown context kind or non-Boolean presence flag was accepted");
    }
    for (int scenario = 0; scenario < 13; ++scenario) {
        auto malformed = saved;
        auto &context = malformed.event_contexts[0];
        if (scenario == 0)
            malformed.global_initialization_pending.reset();
        else if (scenario == 1)
            context.kind = static_cast<LslEventContextKind>(2);
        else if (scenario == 2)
            context.event_wait = LslEventWaitCheckpoint{};
        else if (scenario == 3)
            context.direct_call_expression.reset();
        else if (scenario == 4) {
            context.kind = LslEventContextKind::DequeueBackground;
            context.event_wait = LslEventWaitCheckpoint{12, 100000, 0, 0, 1 << 2, 0};
        } else if (scenario == 5)
            malformed.pending_resume_fault = "";
        else if (scenario == 6)
            malformed.resume_frames[0].method.clear();
        else if (scenario == 7)
            malformed.resume_frames[0].event_wait = LslEventWaitCheckpoint{};
        else if (scenario == 8)
            malformed.resume_frames[1].event_wait.reset();
        else if (scenario == 9)
            malformed.resume_frames[1].complete_restore = true;
        else if (scenario == 10)
            malformed.resume_frames[2].complete_restore = false;
        else if (scenario == 11)
            malformed.resume_frames[0].global_initializer_cursor = malformed.globals.size();
        else
            context.invoked_event->name.clear();
        bool rejected = false;
        try {
            encode_lsl_checkpoint(malformed);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Invalid direct-handler or captured replay state was encoded");
    }
    auto fault = base();
    fault.global_initialization_pending = false;
    fault.active_event = LslEvent{"timer", {}, {}};
    fault.pending_resume_fault = "frame mismatch default state_entry vs default timer";
    const auto faulted = decode_lsl_checkpoint(encode_lsl_checkpoint(fault));
    check(faulted && faulted->pending_resume_fault == fault.pending_resume_fault &&
              encode_lsl_checkpoint(*faulted)[3] == 13,
          "Source-proven deferred resume fault was discarded or treated as a fresh script");
    auto entry = base();
    entry.global_initialization_pending = false;
    entry.active_event = LslEvent{"timer", {}, {}};
    entry.resume_entry_pending = true;
    const auto pending_entry = decode_lsl_checkpoint(encode_lsl_checkpoint(entry));
    check(pending_entry && pending_entry->resume_entry_pending && encode_lsl_checkpoint(*pending_entry)[3] == 13,
          "Pending replay entry was confused with an already-started empty frame queue");
    auto sliced = base();
    sliced.global_initialization_pending = false;
    sliced.active_event = LslEvent{"timer", {}, {}};
    for (const auto elapsed : {std::int64_t{0}, std::int64_t{599999}, std::int64_t{600000}, std::int64_t{610000}}) {
        sliced.execution_slice_elapsed_ticks = elapsed;
        const auto slice = decode_lsl_checkpoint(encode_lsl_checkpoint(sliced));
        check(slice && slice->execution_slice_elapsed_ticks.has_value() &&
                  *slice->execution_slice_elapsed_ticks == elapsed && encode_lsl_checkpoint(*slice)[3] == 13,
              "Logical elapsed slice lost zero or an expired pending source-check interval");
    }
    auto bad_slice = encode_lsl_checkpoint(sliced);
    store_u64(bad_slice, bad_slice.size() - 8, std::numeric_limits<std::uint64_t>::max());
    check(!decode_lsl_checkpoint(bad_slice), "Negative logical elapsed slice was accepted");
    sliced.execution_slice_elapsed_ticks.reset();
    check(encode_lsl_checkpoint(sliced) == version12, "Closing the logical slice changed exact old version12 bytes");
    invalid = version12;
    invalid[3] = 13;
    invalid.insert(invalid.end(), 8, 0);
    check(!decode_lsl_checkpoint(invalid), "Checkpoint13 without any new state was accepted");
    check(encode_lsl_checkpoint(base())[3] == 1 && version12[3] == 12 &&
              encode_lsl_checkpoint(*decode_lsl_checkpoint(version12)) == version12,
          "Direct-handler schema changed exact legacy version12 bytes");
}
} // namespace

int main() {
    try {
        legacy_versions();
        typed_roundtrip_and_rejection();
        nested_contexts();
        boxed_values();
        constructor_contexts();
        direct_handler_contexts();
        std::cout << "PASS: checkpoint13 direct calls/replay, constructor phases, unchanged versions1–12 and bounds\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

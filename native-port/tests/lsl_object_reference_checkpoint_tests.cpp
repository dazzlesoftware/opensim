// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/framework/dotnet_identity_hash.hpp"
#include "opensim/script/lsl_reference_string.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

namespace {
using String = std::shared_ptr<const LslStringReference>;
using Box = std::shared_ptr<const LslObjectBox>;

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

LslCheckpoint base() {
    LslCheckpoint c;
    c.program_hash = "object-codec";
    c.state = "default";
    c.globals = {std::int32_t{1}};
    c.initialized_globals = 1;
    return c;
}

String string(const LslValue &value) {
    return std::get<String>(value);
}

Box box(const LslValue &value) {
    return std::get<Box>(value);
}

void identity_graph() {
    const auto literal = make_lsl_reference_string(u"same", true);
    const auto dynamic = make_lsl_reference_string(u"same");
    const auto another = make_lsl_reference_string(u"same");
    check(string(literal) == string(make_lsl_reference_string(u"same", true)), "Literal pool is not process-wide");
    check(string(dynamic) != string(another) && string(dynamic) != string(literal),
          "Dynamic String was interned by value");
    const auto number = make_lsl_object_box(std::int32_t{7});
    const auto second_number = make_lsl_object_box(std::int32_t{7});
    const auto sdk = make_lsl_object_box(LslBoxedString{"same"}, string(literal));
    check(std::holds_alternative<std::int32_t>(box(number)->value), "Raw Int32 became an SDK wrapper");
    check(!lsl_string_code_units(number), "Numeric Object was textified");
    check(string(unbox_lsl_value(sdk)) == string(literal), "SDK String field identity lost on unbox");
    auto c = base();
    c.globals = {literal, literal, dynamic, dynamic, another, number, number, second_number, sdk, sdk};
    auto child = std::make_shared<const LslList>(LslList{{literal, number, sdk}});
    c.locals = {child, child};
    c.evaluations = {{1, number}};
    auto wire = encode_lsl_checkpoint(c);
    check(wire[3] == 19, "Reference graph did not select schema19");
    auto decoded = decode_lsl_checkpoint(wire);
    check(decoded.has_value(), "Internal reference graph rejected");
    const auto &g = decoded->globals;
    check(string(g[0]) == string(g[1]) && string(g[2]) == string(g[3]), "String aliases lost");
    check(string(g[0]) != string(g[2]) && string(g[2]) != string(g[4]), "Distinct String references merged");
    check(string(g[0]) == string(literal), "Budget restore broke future literal identity");
    check(box(g[5]) == box(g[6]) && box(g[5]) != box(g[7]), "Box identity changed");
    check(box(g[8]) == box(g[9]) && box(g[8])->string_field == string(g[0]), "SDK box/field alias lost");
    auto list = std::get<std::shared_ptr<const LslList>>(decoded->locals[0]);
    check(list == std::get<std::shared_ptr<const LslList>>(decoded->locals[1]), "List DAG alias lost");
    check(string(list->items[0]) == string(g[0]) && box(list->items[1]) == box(g[5]),
          "Cross-root reference alias lost");
    check(encode_lsl_checkpoint(*decoded) == wire, "Internal reference graph is not canonical");
    auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
    check(external.has_value(), "External reference graph rejected");
    const auto &e = external->globals;
    check(string(e[0]) == string(e[2]) && string(e[2]) == string(e[4]), "Binary String content canonicalization lost");
    check(!lsl_reference_string_is_literal(string(e[0])), "External nonempty String became a literal");
    check(box(e[5]) != box(e[6]) && box(e[8]) != box(e[9]), "External CLR value boxes retained aliases");
    check(box(e[8])->string_field == string(e[0]), "External SDK String field did not share binary String");
    check(encode_lsl_checkpoint(c) == wire, "Persistent writer changed live reference graph");
    for (std::size_t n = 0; n < wire.size(); ++n)
        check(!decode_lsl_checkpoint(std::span(wire).first(n)), "Truncated reference graph accepted");
    auto old = wire;
    old[3] = 18;
    check(!decode_lsl_checkpoint(old), "Old schema accepted new reference tags");
}

void pre_encoding_canonicalization() {
    auto empty = make_lsl_reference_string(u"");
    auto other_empty = make_lsl_reference_string(u"");
    check(string(empty) != string(other_empty), "Dynamic empty references were interned");
    auto empty_checkpoint = base();
    empty_checkpoint.globals = {empty, other_empty};
    auto internal = decode_lsl_checkpoint(encode_lsl_checkpoint(empty_checkpoint));
    auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(empty_checkpoint));
    check(internal && string(internal->globals[0]) != string(internal->globals[1]), "Internal empty identity lost");
    check(external && string(external->globals[0]) == string(make_lsl_reference_string(u"", true)) &&
              string(external->globals[0]) == string(external->globals[1]),
          "BinaryReader empty singleton was not restored");
    check(std::get<Box>(make_lsl_object_box(LslBoxedString{""}))->string_field ==
              string(make_lsl_reference_string(u"", true)),
          "SDK empty string field did not use String.Empty");
    auto a = make_lsl_reference_string(std::u16string{0xd800});
    auto b = make_lsl_reference_string(std::u16string{0xd800});
    auto replacement = make_lsl_reference_string(std::u16string{0xfffd});
    auto c = base();
    c.globals = {a, b, replacement};
    auto saved = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
    check(saved.has_value(), "Replacement reference graph rejected");
    check(string(saved->globals[0]) == string(saved->globals[1]), "Original equal strings not canonicalized");
    check(string(saved->globals[0]) != string(saved->globals[2]), "Canonicalization ran after UTF8 replacement");
    for (const auto &value : saved->globals)
        check(string(value)->units == std::u16string{0xfffd}, "External String replacement differs");
    check(string(a)->units == std::u16string{0xd800}, "External String writer mutated live units");
}

void hidden_roots() {
    for (const LslValue ch :
         std::array<LslValue, 2>{make_lsl_reference_string(u"root"), make_lsl_object_box(std::int32_t{7})}) {
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
                check(wire[3] == 19, "Hidden nullable root did not promote version19");
                const auto d = decode_lsl_checkpoint(wire);
                check(d && (persistent ? encode_persistent_lsl_checkpoint(*d) : encode_lsl_checkpoint(*d)) == wire,
                      "Hidden nullable List root roundtrip differs");
            }
        }
    }
}

void list_identity() {
    std::uint32_t state = 123456789u;
    const auto prior = state;
    const auto hash = opensim::framework::dotnet_identity_hash(state, 3);
    check(state == prior * 17u + 1u && hash == state >> 6 && hash, "Explicit identity generator differs");
    auto list = std::make_shared<LslList>();
    list->identity_hash = hash;
    auto c = base();
    c.globals = {std::shared_ptr<const LslList>(list), std::shared_ptr<const LslList>(list)};
    auto wire = encode_lsl_checkpoint(c);
    check(wire[3] == 19, "List identity alone did not select schema19");
    auto restored = decode_lsl_checkpoint(wire);
    check(restored.has_value(), "List identity checkpoint rejected");
    auto a = std::get<std::shared_ptr<const LslList>>(restored->globals[0]);
    check(a->identity_hash == hash && a == std::get<std::shared_ptr<const LslList>>(restored->globals[1]),
          "Internal List identity or alias lost");
    auto persistent = encode_persistent_lsl_checkpoint(c);
    check(persistent[3] != 19, "External hash-only state unnecessarily selected schema19");
    auto loaded = decode_lsl_checkpoint(persistent);
    check(loaded && !std::get<std::shared_ptr<const LslList>>(loaded->globals[0])->identity_hash,
          "External migration retained backing identity hash");
    check(list->identity_hash == hash, "Persistent writer changed live identity");
    for (auto invalid : {0u, 0x4000000u}) {
        list->identity_hash = invalid;
        bool rejected = false;
        try {
            (void)encode_lsl_checkpoint(c);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Invalid cached identity hash accepted");
    }
}

void integer_string_origins() {
    for (std::int32_t n : {0, 1, 299}) {
        const auto cached = make_lsl_cached_integer_string(n);
        const auto units = *lsl_string_code_units(cached);
        const auto literal = make_lsl_reference_string(units, true);
        check(string(cached) != string(literal) && string(cached) == string(make_lsl_cached_integer_string(n)),
              "Integer cache origin merged with literal or became fresh");
        check(lsl_reference_string_cached_integer(string(cached)) == n &&
                  !lsl_reference_string_cached_integer(string(literal)),
              "Integer origin query inferred content");
        auto c = base();
        c.globals = {cached, literal};
        auto internal = decode_lsl_checkpoint(encode_lsl_checkpoint(c));
        auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
        check(internal && string(internal->globals[0]) == string(cached), "Internal cache origin lost");
        check(external && string(external->globals[0]) != string(cached) &&
                  !lsl_reference_string_cached_integer(string(external->globals[0])),
              "External cache origin retained");
    }
    for (std::int32_t n : {-1, 300})
        check(string(make_lsl_cached_integer_string(n)) != string(make_lsl_cached_integer_string(n)),
              "Out-of-range Integer conversion was cached");
}

void invalid_inputs() {
    auto c = base();
    for (const auto &value : std::array<LslValue, 4>{
             String{}, Box{}, std::make_shared<const LslObjectBox>(LslObjectBox{std::string("raw reference"), {}}),
             std::make_shared<const LslObjectBox>(
                 LslObjectBox{std::int32_t{1}, string(make_lsl_reference_string(u"wrong"))})}) {
        c.globals = {value};
        bool rejected = false;
        try {
            encode_lsl_checkpoint(c);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Invalid Object/String carrier accepted");
    }
    bool rejected = false;
    try {
        make_lsl_object_box(make_lsl_object_box(std::int32_t{1}));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "Nested Object box payload accepted");
    c.globals = {make_lsl_reference_string(u"a")};
    auto wire = encode_lsl_checkpoint(c);
    const std::array<std::uint8_t, 9> pattern{23, 0, 0, 1, 0, 0, 0, 97, 0};
    auto at = std::search(wire.begin(), wire.end(), pattern.begin(), pattern.end());
    check(at != wire.end(), "String reference wire layout differs");
    const auto offset = static_cast<std::size_t>(at - wire.begin());
    auto bad = wire;
    bad[offset + 1] = 2;
    check(!decode_lsl_checkpoint(bad), "Unknown reference marker accepted");
    bad = wire;
    bad[offset + 1] = 1;
    bad[offset + 2] = 0;
    bad[offset + 3] = 0;
    bad[offset + 4] = 0;
    bad[offset + 5] = 0;
    check(!decode_lsl_checkpoint(bad), "Forward String reference accepted");
    bad = wire;
    bad[offset + 2] = 3;
    check(!decode_lsl_checkpoint(bad), "Invalid literal flag accepted");
    c = base();
    auto legacy = encode_lsl_checkpoint(c);
    check(legacy[3] == 1, "Reference-free legacy schema changed");
    legacy[3] = 19;
    check(!decode_lsl_checkpoint(legacy), "Schema19 without reference carriers accepted");
    c.globals = {LslRawBoolean{true}};
    check(encode_lsl_checkpoint(c)[3] == 18, "Raw-only schema18 changed");
}
} // namespace

int main() {
    try {
        identity_graph();
        pre_encoding_canonicalization();
        hidden_roots();
        list_identity();
        integer_string_origins();
        invalid_inputs();
        std::cout << "Object reference checkpoint tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

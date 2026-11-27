// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_reference_string.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

namespace {
using String = std::shared_ptr<const LslStringReference>;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

String string(const LslValue &value) {
    return std::get<String>(value);
}

LslCheckpoint base() {
    LslCheckpoint c;
    c.program_hash = "class-name-codec";
    c.state = "default";
    c.globals = {std::int32_t{1}};
    c.initialized_globals = 1;
    return c;
}

LslClassTypeNameOrigin origin(std::string hash = "class-source", std::string name = "integer[]",
                              const char *owner = "11111111-2222-3333-4444-555555555555") {
    LslClassTypeNameOrigin result;
    result.owner = opensim::framework::Uuid(owner);
    result.source_hash = std::move(hash);
    result.canonical_name = std::move(name);
    return result;
}

void graph() {
    const auto name_origin = origin();
    auto name = make_lsl_class_type_name_string(name_origin);
    auto independent = make_lsl_class_type_name_string(name_origin);
    auto literal = make_lsl_reference_string(u"integer[]", true);
    check(string(name) != string(independent) && string(name) != string(literal),
          "Origin factory interned owners globally");
    auto c = base();
    auto foreign =
        make_lsl_class_type_name_string(origin("class-source", "integer[]", "66666666-7777-8888-9999-aaaaaaaaaaaa"));
    c.globals = {name, name, independent, literal, foreign};
    c.class_type_name_owner = name_origin.owner;
    auto list = std::make_shared<const LslList>(LslList{{name, independent}});
    c.locals = {list};
    auto wire = encode_lsl_checkpoint(c);
    check(wire[3] == 22, "Class name did not select schema22");
    auto d = decode_lsl_checkpoint(wire);
    check(bool(d), "Class name graph rejected");
    check(string(d->globals[0]) == string(d->globals[1]), "Class name wire alias split");
    check(string(d->globals[0]) != string(d->globals[2]), "Distinct references merged while decoding");
    check(string(d->globals[0]) != string(d->globals[4]), "Foreign owner merged while decoding");
    check(lsl_reference_string_class_type_name(string(d->globals[4]))->owner != name_origin.owner,
          "Foreign owner metadata lost");
    check(d->class_type_name_owner == name_origin.owner, "Checkpoint owner metadata lost");
    check(string(d->globals[0]) != string(name), "Decoded origin bound to process cache");
    check(lsl_reference_string_class_type_name(string(d->globals[0])) == name_origin, "Origin metadata lost");
    check(std::get<std::shared_ptr<const LslList>>(d->locals[0])->items[0] == d->globals[0], "Hidden List alias split");
    check(encode_lsl_checkpoint(*d) == wire, "Schema22 graph not stable");
    auto externalWire = encode_persistent_lsl_checkpoint(c);
    check(externalWire[3] == 19, "Persistent class name retained origin version");
    auto external = decode_lsl_checkpoint(externalWire);
    check(bool(external), "External class name rejected");
    check(!external->class_type_name_owner, "Persistent checkpoint owner retained");
    for (const auto &value : external->globals) {
        check(string(value) == string(external->globals[0]), "External equal String content not merged");
        check(!lsl_reference_string_class_type_name(string(value)), "External class name origin retained");
    }
    for (std::size_t n = 0; n < wire.size(); ++n)
        check(!decode_lsl_checkpoint(std::span(wire).first(n)), "Truncated schema22 accepted");
    auto old = wire;
    old[3] = 21;
    check(!decode_lsl_checkpoint(old), "Schema21 accepted origin4");
    auto trailing = wire;
    trailing.push_back(0);
    check(!decode_lsl_checkpoint(trailing), "Trailing byte accepted");
    const std::array<std::uint8_t, 3> definition{23, 0, 4};
    auto found = std::search(wire.begin() + 4, wire.end(), definition.begin(), definition.end());
    check(found != wire.end(), "Origin4 definition missing");
    auto offset = static_cast<std::size_t>(found - wire.begin());
    auto invalid = wire;
    invalid[offset + 2] = 5;
    check(!decode_lsl_checkpoint(invalid), "Unknown String origin accepted");
    invalid = wire;
    std::fill(invalid.begin() + offset + 3, invalid.begin() + offset + 19, 0);
    check(!decode_lsl_checkpoint(invalid), "Nil class String owner accepted");
    invalid = wire;
    const auto sourceBytes = name_origin.source_hash.size();
    for (unsigned i = 0; i < 4; ++i)
        invalid[offset + 19 + i] = 0;
    invalid.erase(invalid.begin() + offset + 23, invalid.begin() + offset + 23 + sourceBytes);
    check(!decode_lsl_checkpoint(invalid), "Empty class source origin accepted");
    invalid = wire;
    const auto canonical = offset + 23 + sourceBytes + 4;
    invalid[canonical] = 'x';
    check(!decode_lsl_checkpoint(invalid), "Class name origin/text mismatch accepted");
    c.class_type_name_owner.reset();
    c.globals = {make_lsl_reference_string(u"plain")};
    c.locals.clear();
    check(encode_lsl_checkpoint(c)[3] == 19, "Plain String unnecessarily promoted");
    c.globals = {make_lsl_fixed_array(LslFixedArrayKind::Integer, 1)};
    check(encode_lsl_checkpoint(c)[3] == 20, "Fixed array unnecessarily promoted");
    c.globals = {make_lsl_script_class(make_lsl_script_array_descriptor("class-source", "integer", 1))};
    check(encode_lsl_checkpoint(c)[3] == 21, "Class unnecessarily promoted");
}

void owner_roots() {
    auto c = base();
    c.class_type_name_owner = origin().owner;
    auto wire = encode_lsl_checkpoint(c);
    check(wire[3] == 22, "Checkpoint owner alone did not select schema22");
    auto restored = decode_lsl_checkpoint(wire);
    check(restored && restored->class_type_name_owner == c.class_type_name_owner, "Owner-only checkpoint rejected");
    check(encode_lsl_checkpoint(*restored) == wire, "Owner-only schema22 not stable");
    auto invalid = wire;
    std::fill(invalid.end() - 16, invalid.end(), 0);
    check(!decode_lsl_checkpoint(invalid), "Nil top-level owner accepted");
    c.class_type_name_owner = opensim::framework::Uuid{};
    bool rejected = false;
    try {
        (void)encode_lsl_checkpoint(c);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "Nil top-level owner encoded");
    c.class_type_name_owner.reset();
    auto descriptor = make_lsl_script_array_descriptor("class-source", "integer", 1);
    descriptor->type_name_owner = origin().owner;
    c.globals = {make_lsl_script_class(descriptor)};
    wire = encode_lsl_checkpoint(c);
    check(wire[3] == 22, "Descriptor owner alone did not select schema22");
    restored = decode_lsl_checkpoint(wire);
    using Class = std::shared_ptr<const LslScriptClass>;
    check(restored && std::get<Class>(restored->globals[0])->descriptor->type_name_owner == descriptor->type_name_owner,
          "Descriptor owner lost");
    auto externalWire = encode_persistent_lsl_checkpoint(c);
    check(externalWire[3] == 21, "Persistent descriptor owner retained schema22");
    auto external = decode_lsl_checkpoint(externalWire);
    check(external && std::get<Class>(external->globals[0])->descriptor->type_name_owner.is_zero() &&
              !external->class_type_name_owner,
          "Persistent descriptor owner retained");
}

void class_indices() {
    using Class = std::shared_ptr<const LslScriptClass>;
    auto descriptor =
        std::make_shared<LslScriptClassDescriptor>(*make_lsl_script_array_descriptor("class-source", "integer", 1));
    descriptor->class_index = 0;
    auto c = base();
    c.globals = {make_lsl_script_class(descriptor)};
    for (bool persistent : {false, true}) {
        const auto wire = persistent ? encode_persistent_lsl_checkpoint(c) : encode_lsl_checkpoint(c);
        check(wire[3] == 22, "Class index alone did not select schema22");
        auto restored = decode_lsl_checkpoint(wire);
        check(restored && std::get<Class>(restored->globals[0])->descriptor->class_index == 0, "Class index lost");
        check(!restored->class_type_name_owner &&
                  std::get<Class>(restored->globals[0])->descriptor->type_name_owner.is_zero(),
              "Index-only class invented an owner");
    }
    descriptor->type_name_owner = origin().owner;
    c.class_type_name_owner = origin().owner;
    auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(c));
    check(external && std::get<Class>(external->globals[0])->descriptor->class_index == 0 &&
              std::get<Class>(external->globals[0])->descriptor->type_name_owner.is_zero() &&
              !external->class_type_name_owner,
          "Persistent class index/owner handling differs");
    // Locate the class definition's metadata, then corrupt the signed index.
    auto wire = encode_lsl_checkpoint(c);
    const std::array<std::uint8_t, 2> marker{26, 0};
    auto definition = std::search(wire.begin() + 4, wire.end(), marker.begin(), marker.end());
    check(definition != wire.end(), "Class definition missing");
    auto cursor = static_cast<std::size_t>(definition - wire.begin()) + 2;
    for (unsigned field = 0; field < 3; ++field) {
        std::uint32_t length = 0;
        for (unsigned byte = 0; byte < 4; ++byte)
            length |= std::uint32_t(wire[cursor + byte]) << (byte * 8);
        cursor += 4 + length;
    }
    cursor += 16;
    wire[cursor] = 0xfe;
    for (unsigned byte = 1; byte < 4; ++byte)
        wire[cursor + byte] = 0xff;
    check(!decode_lsl_checkpoint(wire), "Class index below -1 accepted");
    descriptor->class_index = -2;
    bool rejected = false;
    try {
        (void)encode_lsl_checkpoint(c);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "Invalid class index encoded");
}

void hidden_roots() {
    for (const LslValue ch :
         std::array<LslValue, 1>{make_lsl_class_type_name_string(origin("hidden-source", "integer[]"))}) {
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
                check(wire[3] == (persistent ? 19 : 22), "Hidden class name root selected wrong version");
                const auto d = decode_lsl_checkpoint(wire);
                check(d && (persistent ? encode_persistent_lsl_checkpoint(*d) : encode_lsl_checkpoint(*d)) == wire,
                      "Hidden class name root roundtrip differs");
            }
        }
    }
}

} // namespace

int main() {
    try {
        graph();
        hidden_roots();
        owner_roots();
        class_indices();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}

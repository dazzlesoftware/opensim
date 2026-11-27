// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/lsl_reference_string.hpp"
#include "opensim/script/lsl_fixed_array.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
opensim::network::Value checkpoint_value(const opensim::script::LslValue &);

opensim::network::Value component_bits(std::initializer_list<double> components) {
    using opensim::network::Value;
    Value result;
    result.kind = Value::Kind::Array;
    for (const auto component : components) {
        std::ostringstream text;
        text << std::hex << std::setfill('0') << std::setw(16) << std::bit_cast<std::uint64_t>(component);
        result.array.emplace_back(text.str());
    }
    return result;
}

opensim::network::Value detector_values(const std::vector<opensim::script::LslDetector> &detectors) {
    using opensim::network::Value;
    Value result;
    result.kind = Value::Kind::Array;
    for (const auto &detector : detectors) {
        Value name_units;
        name_units.kind = Value::Kind::Array;
        const auto units = detector.sdk_name ? *detector.sdk_name : opensim::framework::utf16(detector.name);
        for (const auto unit : units)
            name_units.array.emplace_back(static_cast<std::int32_t>(unit));
        const auto vector_bits = [](const auto &vector) { return component_bits({vector.x, vector.y, vector.z}); };
        result.array.push_back(Value::object({
            {"name", Value(detector.name)},
            {"name_units", std::move(name_units)},
            {"key", Value(detector.key.str())},
            {"owner", Value(detector.owner.str())},
            {"group", Value(detector.group.str())},
            {"type", Value(detector.type)},
            {"link_number", Value(detector.link_number)},
            {"touch_face", Value(detector.touch_face)},
            {"position_bits", vector_bits(detector.position)},
            {"velocity_bits", vector_bits(detector.velocity)},
            {"grab_bits", vector_bits(detector.grab)},
            {"touch_pos_bits", vector_bits(detector.touch_pos)},
            {"touch_normal_bits", vector_bits(detector.touch_normal)},
            {"touch_binormal_bits", vector_bits(detector.touch_binormal)},
            {"touch_st_bits", vector_bits(detector.touch_st)},
            {"touch_uv_bits", vector_bits(detector.touch_uv)},
            {"rotation_bits",
             component_bits({detector.rotation.x, detector.rotation.y, detector.rotation.z, detector.rotation.w})},
        }));
    }
    return result;
}

opensim::network::Value detector_state(const opensim::script::LslCheckpoint &state) {
    using opensim::network::Value;
    Value active, queued, backgrounds;
    active.kind = queued.kind = backgrounds.kind = Value::Kind::Array;
    if (state.active_event)
        active = detector_values(state.active_event->detectors);
    for (const auto &event : state.queued_events) {
        Value arguments, kinds, aliases;
        arguments.kind = kinds.kind = aliases.kind = Value::Kind::Array;
        for (const auto &argument : event.arguments) {
            arguments.array.push_back(checkpoint_value(argument));
            kinds.array.push_back(Value::long_integer(static_cast<std::int64_t>(argument.index())));
        }
        for (std::size_t i = 0; i < event.arguments.size(); ++i) {
            const auto left = std::get_if<std::shared_ptr<const opensim::script::LslList>>(&event.arguments[i]);
            if (!left)
                continue;
            for (std::size_t j = i + 1; j < event.arguments.size(); ++j) {
                const auto right = std::get_if<std::shared_ptr<const opensim::script::LslList>>(&event.arguments[j]);
                if (right)
                    aliases.array.push_back(Value::object({
                        {"left", Value::long_integer(static_cast<std::int64_t>(i))},
                        {"right", Value::long_integer(static_cast<std::int64_t>(j))},
                        {"same_list", Value(*left == *right)},
                    }));
            }
        }
        queued.array.push_back(Value::object({{"event", Value(event.name)},
                                              {"detectors", detector_values(event.detectors)},
                                              {"arguments", std::move(arguments)},
                                              {"argument_kinds", std::move(kinds)},
                                              {"list_aliases", std::move(aliases)}}));
    }
    for (std::size_t i = 0; i < state.resume_frames.size(); ++i) {
        const auto &frame = state.resume_frames[i];
        if (frame.background_event)
            backgrounds.array.push_back(Value::object({
                {"index", Value::long_integer(static_cast<std::int64_t>(i))},
                {"event", Value(frame.background_event->name)},
                {"return_mask1", Value(frame.event_wait->return_mask1)},
                {"return_mask2", Value(frame.event_wait->return_mask2)},
                {"background_mask1", Value(frame.event_wait->background_mask1)},
                {"background_mask2", Value(frame.event_wait->background_mask2)},
                {"detectors", detector_values(frame.background_event->detectors)},
            }));
    }
    return Value::object(
        {{"active", std::move(active)}, {"queued", std::move(queued)}, {"background_frames", std::move(backgrounds)}});
}

opensim::network::Value checkpoint_value_graph(const opensim::script::LslValue &value,
                                               std::map<const opensim::script::LslFixedArray *, std::int32_t> &arrays) {
    using opensim::network::Value;
    if (const auto *box = std::get_if<std::shared_ptr<const opensim::script::LslObjectBox>>(&value)) {
        if (!*box || !opensim::script::valid_lsl_object_box(**box))
            throw std::runtime_error("Invalid Object box in checkpoint probe");
        return checkpoint_value_graph((*box)->value, arrays);
    }
    if (const auto *reference = std::get_if<std::shared_ptr<const opensim::script::LslStringReference>>(&value)) {
        if (!*reference)
            throw std::runtime_error("Invalid String reference in checkpoint probe");
        Value result(opensim::framework::utf8((*reference)->units));
        result.sdk_string = std::make_shared<const std::u16string>((*reference)->units);
        return result;
    }
    if (opensim::script::is_null_lsl_string(value) || std::holds_alternative<opensim::script::LslNullList>(value) ||
        std::holds_alternative<opensim::script::LslNullObject>(value))
        return Value{};
    if ((value.index() >= 6 && value.index() < 10) ||
        std::holds_alternative<opensim::script::LslBoxedUtf16String>(value) ||
        std::holds_alternative<opensim::script::LslBoxedUtf16Key>(value))
        return checkpoint_value_graph(opensim::script::unbox_lsl_value(value), arrays);
    if (const auto text = std::get_if<opensim::script::LslUtf16String>(&value)) {
        Value result(opensim::framework::utf8(text->value));
        result.sdk_string = std::make_shared<const std::u16string>(text->value);
        return result;
    }
    if (const auto character = std::get_if<opensim::script::LslCharacter>(&value))
        return Value(static_cast<std::int32_t>(character->value));
    if (const auto integer = std::get_if<std::int32_t>(&value))
        return Value(*integer);
    if (const auto text = std::get_if<std::string>(&value))
        return Value(*text);
    const auto real = [](double number) {
        Value result;
        result.kind = Value::Kind::Real;
        result.real = number;
        return result;
    };
    if (const auto number = std::get_if<double>(&value))
        return real(*number);
    if (const auto number = std::get_if<opensim::script::LslRawSingle>(&value))
        return real(number->value);
    if (const auto boolean = std::get_if<opensim::script::LslRawBoolean>(&value))
        return Value(boolean->value);
    if (const auto uuid = std::get_if<opensim::script::LslRawUuid>(&value))
        return Value(uuid->value.str());
    if (const auto *array = std::get_if<std::shared_ptr<const opensim::script::LslFixedArray>>(&value)) {
        if (!*array)
            throw std::runtime_error("Invalid fixed array in checkpoint probe");
        const auto next = static_cast<std::int32_t>(arrays.size() + 1);
        const auto [found, inserted] = arrays.try_emplace(array->get(), next);
        if (!inserted)
            return Value::object({{"array_reference", Value(found->second)}});
        Value items, bits;
        items.kind = bits.kind = Value::Kind::Array;
        for (std::size_t index = 0; index < opensim::script::lsl_fixed_array_size(*array); ++index) {
            const auto item = opensim::script::lsl_fixed_array_get(*array, index);
            items.array.push_back(checkpoint_value_graph(item, arrays));
            if (const auto *number = std::get_if<double>(&item))
                bits.array.push_back(component_bits({*number}).array.front());
        }
        return Value::object({{"fixed_array", Value(found->second)},
                              {"element_kind", Value(static_cast<std::int32_t>((*array)->kind))},
                              {"items", std::move(items)},
                              {"double_bits", std::move(bits)}});
    }
    Value result;
    result.kind = Value::Kind::Array;
    if (const auto vector = std::get_if<opensim::framework::Vector3d>(&value))
        result.array = {real(vector->x), real(vector->y), real(vector->z)};
    else if (const auto rotation = std::get_if<opensim::script::LslRotation>(&value))
        result.array = {real(rotation->x), real(rotation->y), real(rotation->z), real(rotation->w)};
    else if (const auto list = std::get_if<std::shared_ptr<const opensim::script::LslList>>(&value); list && *list)
        for (const auto &element : (*list)->items)
            result.array.push_back(checkpoint_value_graph(element, arrays));
    return result;
}

opensim::network::Value checkpoint_value(const opensim::script::LslValue &value) {
    std::map<const opensim::script::LslFixedArray *, std::int32_t> arrays;
    return checkpoint_value_graph(value, arrays);
}

class ReferenceProbe {
    std::map<const void *, std::int32_t> ids_;
    std::set<const opensim::script::LslList *> lists_;
    std::set<const opensim::script::LslFixedArray *> arrays_;

    std::int32_t id(const void *pointer) {
        if (!pointer)
            return 0;
        const auto next = static_cast<std::int32_t>(ids_.size() + 1);
        return ids_.try_emplace(pointer, next).first->second;
    }

  public:
    opensim::network::Value inspect(const opensim::script::LslValue &value) {
        using namespace opensim::script;
        using opensim::network::Value;
        if (const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value)) {
            if (!*array)
                throw std::runtime_error("Invalid fixed array reference in checkpoint probe");
            const auto identity = id(array->get());
            Value items;
            items.kind = Value::Kind::Array;
            if (arrays_.insert(array->get()).second)
                for (std::size_t index = 0; index < lsl_fixed_array_size(*array); ++index)
                    items.array.push_back(inspect(lsl_fixed_array_get(*array, index)));
            return Value::object(
                {{"kind", Value("fixed_array")},
                 {"reference", Value(identity)},
                 {"element_kind", Value(static_cast<std::int32_t>((*array)->kind))},
                 {"length", Value::long_integer(lsl_fixed_array_size(*array))},
                 {"identity_hash",
                  (*array)->identity_hash ? Value(static_cast<std::int32_t>(*(*array)->identity_hash)) : Value{}},
                 {"items", std::move(items)}});
        }
        if (const auto *reference = std::get_if<std::shared_ptr<const LslStringReference>>(&value)) {
            const auto cached = lsl_reference_string_cached_integer(*reference);
            const auto array_type = lsl_reference_string_cached_array_type(*reference);
            return Value::object(
                {{"kind", Value("string")},
                 {"reference", Value(id(reference->get()))},
                 {"literal", Value(lsl_reference_string_is_literal(*reference))},
                 {"small_integer_cache", cached ? Value(*cached) : Value{}},
                 {"array_type_cache", array_type ? Value(static_cast<std::int32_t>(*array_type)) : Value{}}});
        }
        if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value)) {
            if (!*box || !valid_lsl_object_box(**box))
                throw std::runtime_error("Invalid Object reference in checkpoint probe");
            return Value::object({{"kind", Value("box")},
                                  {"reference", Value(id(box->get()))},
                                  {"payload_kind", Value::long_integer((*box)->value.index())},
                                  {"string_field_reference", Value(id((*box)->string_field.get()))}});
        }
        if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(&value); list && *list) {
            Value items;
            items.kind = Value::Kind::Array;
            const auto identity = id(list->get());
            if (lists_.insert(list->get()).second)
                for (const auto &item : (*list)->items)
                    items.array.push_back(inspect(item));
            return Value::object(
                {{"kind", Value("list")},
                 {"reference", Value(identity)},
                 {"identity_hash",
                  (*list)->identity_hash ? Value(static_cast<std::int32_t>(*(*list)->identity_hash)) : Value{}},
                 {"items", std::move(items)}});
        }
        return Value::object({{"kind", Value("value")}, {"variant", Value::long_integer(value.index())}});
    }
};

int inspect_checkpoint(const char *path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Could not open native checkpoint");
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto state = opensim::script::decode_lsl_checkpoint(bytes);
    if (!state)
        throw std::runtime_error("Native checkpoint rejected");
    using opensim::network::Value;
    Value globals;
    globals.kind = Value::Kind::Array;
    for (const auto &value : state->globals)
        globals.array.push_back(checkpoint_value(value));
    ReferenceProbe reference_probe;
    Value references;
    references.kind = Value::Kind::Array;
    for (const auto &value : state->globals)
        references.array.push_back(reference_probe.inspect(value));
    Value string_units;
    string_units.kind = Value::Kind::Array;
    for (const auto &value : state->globals) {
        Value units;
        if (const auto text = opensim::script::lsl_string_code_units(value)) {
            units.kind = Value::Kind::Array;
            for (const auto unit : *text)
                units.array.emplace_back(static_cast<std::int32_t>(unit));
        }
        string_units.array.push_back(std::move(units));
    }
    Value rotation_bits;
    rotation_bits.kind = Value::Kind::Array;
    for (const auto &value : state->globals) {
        Value bits;
        if (const auto rotation = std::get_if<opensim::script::LslRotation>(&value)) {
            bits.kind = Value::Kind::Array;
            for (const auto component : {rotation->x, rotation->y, rotation->z, rotation->w}) {
                std::ostringstream text;
                text << std::hex << std::setfill('0') << std::setw(16) << std::bit_cast<std::uint64_t>(component);
                bits.array.emplace_back(text.str());
            }
        }
        rotation_bits.array.push_back(std::move(bits));
    }
    std::array<std::int32_t, 4> boxed_counts{};
    std::array<std::int32_t, 3> null_counts{};
    std::array<std::int32_t, 2> reference_null_counts{};
    std::int32_t character_count = 0;
    std::set<const opensim::script::LslList *> visited;
    std::vector<const opensim::script::LslValue *> pending;
    for (const auto &value : state->globals)
        pending.push_back(&value);
    while (!pending.empty()) {
        const auto &value = *pending.back();
        pending.pop_back();
        if (std::holds_alternative<opensim::script::LslNullList>(value)) {
            ++reference_null_counts[0];
        } else if (std::holds_alternative<opensim::script::LslNullObject>(value)) {
            ++reference_null_counts[1];
        } else if (std::holds_alternative<opensim::script::LslNullString>(value)) {
            ++null_counts[0];
        } else if (std::holds_alternative<opensim::script::LslBoxedNullString>(value)) {
            ++null_counts[1];
            ++boxed_counts[2];
        } else if (std::holds_alternative<opensim::script::LslBoxedNullKey>(value)) {
            ++null_counts[2];
            ++boxed_counts[3];
        } else if (value.index() >= 6 && value.index() < 10) {
            ++boxed_counts.at(value.index() - 6);
        } else if (std::holds_alternative<opensim::script::LslBoxedUtf16String>(value)) {
            ++boxed_counts[2];
        } else if (std::holds_alternative<opensim::script::LslBoxedUtf16Key>(value)) {
            ++boxed_counts[3];
        } else if (std::holds_alternative<opensim::script::LslCharacter>(value)) {
            ++character_count;
        } else if (const auto list = std::get_if<std::shared_ptr<const opensim::script::LslList>>(&value);
                   list && *list && visited.insert(list->get()).second) {
            for (const auto &item : (*list)->items)
                pending.push_back(&item);
        }
    }
    Value local_charges;
    local_charges.kind = Value::Kind::Array;
    for (const auto charge : state->local_object_heap_usage)
        local_charges.array.push_back(charge ? Value(*charge) : Value{});
    const auto result = Value::object({
        {"program_hash", Value(state->program_hash)},
        {"state", Value(state->state)},
        {"globals", std::move(globals)},
        {"global_references", std::move(references)},
        {"arrays_heap_usage", state->arrays_heap_usage ? Value(*state->arrays_heap_usage) : Value{}},
        {"locals_heap_usage", state->locals_heap_usage ? Value(*state->locals_heap_usage) : Value{}},
        {"local_heap_base", state->local_heap_base ? Value(*state->local_heap_base) : Value{}},
        {"local_object_heap_usage", std::move(local_charges)},
        {"heap_restore_pending", Value(state->heap_restore_pending)},
        {"global_string_units", std::move(string_units)},
        {"global_rotation_bits", std::move(rotation_bits)},
        {"detector_state", detector_state(*state)},
        {"global_character_count", Value(character_count)},
        {"global_null_counts", Value::object({{"raw_string", Value(null_counts[0])},
                                              {"boxed_string", Value(null_counts[1])},
                                              {"boxed_key", Value(null_counts[2])}})},
        {"global_reference_null_counts",
         Value::object({{"list", Value(reference_null_counts[0])}, {"object", Value(reference_null_counts[1])}})},
        {"global_boxed_counts", Value::object({{"integer", Value(boxed_counts[0])},
                                               {"float", Value(boxed_counts[1])},
                                               {"string", Value(boxed_counts[2])},
                                               {"key", Value(boxed_counts[3])}})},
        {"running", Value(state->running)},
        {"active_event", Value(state->active_event ? state->active_event->name : "")},
        {"function", Value(state->function)},
        {"call_stack_depth", Value::long_integer(static_cast<std::int64_t>(state->call_stack.size()))},
        {"queued_event_count", Value::long_integer(static_cast<std::int64_t>(state->queued_events.size()))},
        {"instruction", Value::long_integer(static_cast<std::int64_t>(state->instruction))},
        {"initialized_globals", Value::long_integer(static_cast<std::int64_t>(state->initialized_globals))},
        {"global_initialization_state", Value(state->global_initialization_pending.has_value())},
        {"global_initialization_pending", Value(state->global_initialization_pending.value_or(false))},
        {"global_initializer_cursor",
         Value::long_integer(
             state->global_initializer_cursor ? static_cast<std::int64_t>(*state->global_initializer_cursor) : -1)},
        {"sleep_remaining", Value::long_integer(state->sleep_remaining)},
        {"sleep_requested", Value(state->sleep_requested)},
        {"event_wait", Value(state->event_wait.has_value())},
        {"event_wait_expression",
         Value::long_integer(state->event_wait ? static_cast<std::int64_t>(state->event_wait->expression) : 0)},
        {"event_wait_deadline_utc_ticks",
         Value::long_integer(state->event_wait ? state->event_wait->deadline_utc_ticks : 0)},
        {"event_wait_return_mask1", Value(state->event_wait ? state->event_wait->return_mask1 : 0)},
        {"event_wait_return_mask2", Value(state->event_wait ? state->event_wait->return_mask2 : 0)},
        {"event_wait_background_mask1", Value(state->event_wait ? state->event_wait->background_mask1 : 0)},
        {"event_wait_background_mask2", Value(state->event_wait ? state->event_wait->background_mask2 : 0)},
        {"event_context_depth", Value::long_integer(static_cast<std::int64_t>(state->event_contexts.size()))},
        {"resume_frame_count", Value::long_integer(static_cast<std::int64_t>(state->resume_frames.size()))},
        {"resume_entry_pending", Value(state->resume_entry_pending)},
        {"execution_slice_active", Value(state->execution_slice_elapsed_ticks.has_value())},
        {"execution_slice_elapsed_ticks", Value::long_integer(state->execution_slice_elapsed_ticks.value_or(-1))},
        {"pending_resume_fault", Value(state->pending_resume_fault.value_or(""))},
        {"capture_point", Value(state->capture_point.has_value())},
        {"capture_point_instruction",
         Value::long_integer(state->capture_point ? static_cast<std::int64_t>(state->capture_point->instruction) : -1)},
        {"outer_event", Value(state->event_contexts.empty() ? "" : state->event_contexts.front().active_event.name)},
        {"outer_global_initializer_cursor",
         Value::long_integer(!state->event_contexts.empty() && state->event_contexts.front().global_initializer_cursor
                                 ? static_cast<std::int64_t>(*state->event_contexts.front().global_initializer_cursor)
                                 : -1)},
        {"outer_event_wait_return_mask1",
         Value(state->event_contexts.empty() || !state->event_contexts.front().event_wait
                   ? 0
                   : state->event_contexts.front().event_wait->return_mask1)},
        {"outer_event_wait_background_mask1",
         Value(state->event_contexts.empty() || !state->event_contexts.front().event_wait
                   ? 0
                   : state->event_contexts.front().event_wait->background_mask1)},
    });
    std::cout << opensim::network::osd_json(result, true) << '\n';
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--checkpoint") {
        try {
            return inspect_checkpoint(argv[2]);
        } catch (const std::exception &failure) {
            std::cerr << failure.what() << '\n';
            return 2;
        }
    }
    std::string source;
    while (std::getline(std::cin, source)) {
        const auto compiled = opensim::script::compile_lsl(source);
        if (argc == 1) {
            std::cout << (compiled ? "1" : "0") << '\n';
            continue;
        }
        opensim::network::Value values;
        values.kind = opensim::network::Value::Kind::Array;
        if (!compiled) {
            std::cout << "null\n";
            continue;
        }
        opensim::script::LslHost host;
        if (argc > 2)
            host.stack_limit_bytes = std::stoi(argv[2]);
        opensim::script::LslInstance instance(compiled.program, {}, std::move(host));
        auto result = instance.run(100000);
        if (result.status != opensim::script::LslRunStatus::Idle) {
            std::cout << "null\n";
            continue;
        }
        const auto checkpoint = instance.checkpoint();
        if (!checkpoint)
            return 3;
        for (const auto &value : checkpoint->globals) {
            if (opensim::script::is_null_lsl_string(value) ||
                std::holds_alternative<opensim::script::LslNullList>(value) ||
                std::holds_alternative<opensim::script::LslNullObject>(value))
                values.array.emplace_back();
            else if (std::holds_alternative<opensim::script::LslUtf16String>(value) ||
                     std::holds_alternative<std::shared_ptr<const opensim::script::LslStringReference>>(value) ||
                     std::holds_alternative<std::shared_ptr<const opensim::script::LslObjectBox>>(value))
                values.array.push_back(checkpoint_value(value));
            else if (const auto integer = std::get_if<std::int32_t>(&value))
                values.array.emplace_back(*integer);
            else if (const auto character = std::get_if<opensim::script::LslCharacter>(&value))
                values.array.emplace_back(static_cast<std::int32_t>(character->value));
            else if (const auto real = std::get_if<double>(&value)) {
                opensim::network::Value number;
                number.kind = opensim::network::Value::Kind::Real;
                number.real = *real;
                values.array.push_back(std::move(number));
            } else if (const auto text = std::get_if<std::string>(&value))
                values.array.emplace_back(*text);
            else
                return 2; // The reference corpus deliberately uses scalar globals.
        }
        std::cout << opensim::network::osd_json(values, true) << '\n';
    }
}

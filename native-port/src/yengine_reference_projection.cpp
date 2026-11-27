// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/yengine_reference_projection.hpp"
#include "opensim/script/lsl_fixed_array.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include "opensim/framework/text.hpp"
#include <map>
#include <set>
#include <stdexcept>

namespace opensim::script {
struct YEngineReferenceProjection::State {
    struct CollectArraysAfterMaps {
        ~CollectArraysAfterMaps() {
            collect_lsl_fixed_arrays();
        }
    } collect_arrays_after_maps;

    using Kind = YEngineManagedValue::Kind;
    YEngineScriptClassProjectionContext class_context;
    bool failed = false;
    std::map<const YEngineManagedValue *, std::shared_ptr<const LslScriptClass>> classes;

    std::map<const YEngineManagedValue *, std::shared_ptr<const LslStringReference>> strings;
    std::map<const YEngineManagedValue *, std::shared_ptr<const LslList>> lists;
    std::map<const YEngineManagedValue *, LslValue> boxes;
    std::map<const YEngineManagedValue *, LslValue> arrays;
    std::set<const YEngineManagedValue *> active;

    std::optional<LslValue> string(const YEngineManagedRef &raw) {
        if (!raw)
            return {};
        if (raw->kind == Kind::Null)
            return LslNullString{};
        if (raw->kind == Kind::LslString || raw->kind == Kind::LslKey) {
            const auto *field = std::get_if<YEngineManagedRef>(&raw->value);
            return field ? string(*field) : std::nullopt;
        }
        if (raw->kind != Kind::String)
            return {};
        if (const auto found = strings.find(raw.get()); found != strings.end())
            return found->second;
        const auto *text = std::get_if<std::string>(&raw->value);
        if (!text)
            return {};
        const auto units = framework::utf16_replacing(*text);
        // BinaryReader.ReadString returns String.Empty for a zero byte length.
        // Nonempty received strings are not CLR-interned, even if ldstr later
        // evaluates an equal string literal in the resumed method.
        auto value = make_lsl_reference_string(units, units.empty());
        strings.emplace(raw.get(), std::get<std::shared_ptr<const LslStringReference>>(value));
        return value;
    }

    static std::optional<LslValue> scalar(const YEngineManagedRef &raw) {
        switch (raw->kind) {
        case Kind::Integer:
            if (const auto *value = std::get_if<std::int32_t>(&raw->value))
                return *value;
            break;
        case Kind::LslInteger:
            if (const auto *value = std::get_if<std::int32_t>(&raw->value))
                return LslBoxedInteger{*value};
            break;
        case Kind::Double:
            if (const auto *value = std::get_if<double>(&raw->value))
                return *value;
            break;
        case Kind::LslFloat:
            if (const auto *value = std::get_if<double>(&raw->value))
                return LslBoxedFloat{*value};
            break;
        case Kind::Float:
            if (const auto *value = std::get_if<float>(&raw->value))
                return LslRawSingle{*value};
            break;
        case Kind::Boolean:
            if (const auto *value = std::get_if<bool>(&raw->value))
                return LslRawBoolean{*value};
            break;
        case Kind::Character:
            if (const auto *value = std::get_if<char16_t>(&raw->value))
                return LslCharacter{*value};
            break;
        case Kind::LslVector:
            if (const auto *value = std::get_if<framework::Vector3d>(&raw->value))
                return *value;
            break;
        case Kind::LslRotation:
            if (const auto *value = std::get_if<LslRotation>(&raw->value)) {
                auto rotation = *value;
                if (rotation.x == 0. && rotation.y == 0. && rotation.z == 0. && rotation.w == 0.)
                    rotation.w = 1.;
                return rotation;
            }
            break;
        default:
            break;
        }
        return {};
    }

    std::optional<LslValue> list(const YEngineManagedRef &raw, unsigned depth) {
        if (raw->kind == Kind::Null)
            return LslNullList{};
        if (raw->kind != Kind::LslList)
            return {};
        if (const auto found = lists.find(raw.get()); found != lists.end())
            return found->second;
        const auto *backing = std::get_if<YEngineManagedRef>(&raw->value);
        if (!backing || !*backing)
            return {};
        auto result = std::make_shared<LslList>();
        std::shared_ptr<const LslList> value = result;
        lists.emplace(raw.get(), value);
        if ((*backing)->kind != Kind::Null) {
            if ((*backing)->kind != Kind::Array || (*backing)->element_type != "ob")
                return {};
            const auto *items = std::get_if<YEngineManagedArray>(&(*backing)->value);
            if (!items)
                return {};
            result->items.reserve(items->size());
            for (const auto &item : *items) {
                auto value = project(item, YEngineReferenceType::Object, depth + 1);
                if (!value)
                    return {};
                result->items.push_back(std::move(*value));
            }
        }
        return value;
    }

    std::optional<LslValue> array(const YEngineManagedRef &raw, unsigned depth) {
        if (const auto found = arrays.find(raw.get()); found != arrays.end())
            return found->second;
        const auto *items = std::get_if<YEngineManagedArray>(&raw->value);
        if (!items)
            return {};
        // RecvObjValue resolves the CLR element type, then returns null for
        // every empty SYSARRAY, before allocating a supported native carrier.
        // Preserve this for known primitive/reference/jagged types and aliases.
        if (items->empty()) {
            std::string_view element = raw->element_type;
            while (element.ends_with("[]"))
                element.remove_suffix(2);
            constexpr std::array<std::string_view, 13> known{"bo", "ch", "de", "do", "fl", "in", "li",
                                                             "ob", "ro", "sc", "st", "ve", "xa"};
            if (std::ranges::find(known, element) == known.end())
                return {};
            arrays.emplace(raw.get(), LslNullObject{});
            return LslNullObject{};
        }
        LslFixedArrayKind kind;
        if (raw->element_type == "ch")
            kind = LslFixedArrayKind::Character;
        else if (raw->element_type == "do")
            kind = LslFixedArrayKind::Float;
        else if (raw->element_type == "in")
            kind = LslFixedArrayKind::Integer;
        else if (raw->element_type == "ob")
            kind = LslFixedArrayKind::Object;
        else
            return {};
        auto value = make_lsl_fixed_array(kind, items->size());
        arrays.emplace(raw.get(), value);
        const auto target = std::get<std::shared_ptr<const LslFixedArray>>(value);
        for (std::size_t index = 0; index < items->size(); ++index) {
            const auto &item = (*items)[index];
            std::optional<LslValue> element;
            if (kind == LslFixedArrayKind::Object)
                element = project(item, YEngineReferenceType::Object, depth + 1);
            else if (item && ((kind == LslFixedArrayKind::Character && item->kind == Kind::Character) ||
                              (kind == LslFixedArrayKind::Float && item->kind == Kind::Double) ||
                              (kind == LslFixedArrayKind::Integer && item->kind == Kind::Integer)))
                element = scalar(item);
            if (!element)
                return {};
            lsl_fixed_array_set(target, index, *element);
        }
        return value;
    }

    std::optional<LslValue> script_class(const YEngineManagedRef &raw, unsigned depth) {
        if (raw->kind == Kind::Null)
            return LslNullObject{};
        if (raw->kind != Kind::ScriptClass)
            return {};
        if (auto found = classes.find(raw.get()); found != classes.end())
            return found->second;
        const auto *source = std::get_if<YEngineManagedScriptClass>(&raw->value);
        if (!source)
            return {};
        const auto found = class_context.classes.find(source->class_index);
        if (found == class_context.classes.end() || !found->second)
            return {};
        const auto &descriptor = *found->second;
        std::array<std::uint32_t, 11> sizes{};
        sizes[3] = descriptor.array_rank;
        sizes[5] = 1;
        constexpr std::array<std::string_view, 11> codes{"xa", "ch", "do", "in", "li",  "ob",
                                                         "ro", "st", "ve", "sc", "de[]"};
        if (!supported_lsl_primitive_array_descriptor(descriptor.canonical_name, descriptor.element_type,
                                                      descriptor.array_rank) ||
            descriptor.instance_sizes != sizes || source->instance_arrays.size() != 11)
            return {};
        // The newly created instance runs ClearOldArrays before receiving its
        // fields, so its zero own total must also satisfy the current phase.
        if (class_context.check_heap_total && !class_context.check_heap_total(0))
            return {};
        for (std::size_t lane = 0; lane < 11; ++lane) {
            const auto &array = source->instance_arrays[lane];
            if (!array)
                return {};
            if (array->kind == Kind::Null) {
                if (sizes[lane])
                    return {};
                continue;
            }
            const auto *items = std::get_if<YEngineManagedArray>(&array->value);
            if (array->kind != Kind::Array || array->element_type != codes[lane] || !items ||
                items->size() != sizes[lane])
                return {};
        }
        const auto &dimensions = std::get<YEngineManagedArray>(source->instance_arrays[3]->value);
        for (const auto &dimension : dimensions)
            if (!dimension || dimension->kind != Kind::Integer ||
                !std::holds_alternative<std::int32_t>(dimension->value))
                return {};
        auto result = std::get<std::shared_ptr<const LslScriptClass>>(make_lsl_script_class(found->second));
        // Preregister before projecting Object fields, including a reference
        // through an Object[] back to this wrapper.
        classes.emplace(raw.get(), result);
        for (std::size_t i = 0; i < dimensions.size(); ++i)
            result->instance_arrays[3][i] = std::get<std::int32_t>(dimensions[i]->value);
        const auto &objects = std::get<YEngineManagedArray>(source->instance_arrays[5]->value);
        auto value = project(objects[0], YEngineReferenceType::Object, depth + 1);
        if (!value)
            return {};
        result->instance_arrays[5][0] = std::move(*value);
        const auto usage = std::bit_cast<std::int32_t>(
            std::uint32_t{4} * descriptor.array_rank +
            static_cast<std::uint32_t>(lsl_script_class_object_size(result->instance_arrays[5][0])));
        if (class_context.check_heap_total && !class_context.check_heap_total(usage))
            return {};
        result->arrays_heap_usage = usage;
        result->identity_hash.reset();
        return result;
    }

    std::optional<LslValue> project(const YEngineManagedRef &raw, YEngineReferenceType type, unsigned depth = 0) {
        if (!raw || depth >= 64)
            return {};
        if (type == YEngineReferenceType::ScriptClass ||
            (type == YEngineReferenceType::Object && raw->kind == Kind::ScriptClass))
            return script_class(raw, depth);
        if (type == YEngineReferenceType::Object && raw->kind == Kind::Array)
            return array(raw, depth);
        if (type == YEngineReferenceType::List ||
            (type == YEngineReferenceType::Object && raw->kind == Kind::LslList)) {
            if (const auto found = lists.find(raw.get()); found != lists.end())
                return found->second;
        }
        if (!active.insert(raw.get()).second)
            return {};

        struct Release {
            std::set<const YEngineManagedValue *> &active;
            const YEngineManagedValue *value;

            ~Release() {
                active.erase(value);
            }
        } release{active, raw.get()};

        if (type == YEngineReferenceType::String || type == YEngineReferenceType::Key)
            return string(raw);
        if (type == YEngineReferenceType::List)
            return list(raw, depth);
        if (type == YEngineReferenceType::Object) {
            if (raw->kind == Kind::Null)
                return LslNullObject{};
            if (raw->kind == Kind::String)
                return string(raw);
            if (raw->kind == Kind::LslList)
                return list(raw, depth);
            if (const auto found = boxes.find(raw.get()); found != boxes.end())
                return found->second;
            std::optional<LslValue> payload;
            std::shared_ptr<const LslStringReference> string_field;
            if (raw->kind == Kind::LslString || raw->kind == Kind::LslKey) {
                auto field = string(raw);
                if (!field)
                    return {};
                if (std::holds_alternative<LslNullString>(*field))
                    payload = LslBoxedNullString{};
                else {
                    string_field = std::get<std::shared_ptr<const LslStringReference>>(*field);
                    // Original LSL_Key aliases LSLString in the binary engine.
                    payload = box_lsl_string(string_field->units);
                }
            } else
                payload = scalar(raw);
            if (!payload)
                return {};
            auto box = make_lsl_object_box(std::move(*payload), std::move(string_field));
            boxes.emplace(raw.get(), box);
            return box;
        }
        auto value = scalar(raw);
        if (!value)
            return {};
        switch (type) {
        case YEngineReferenceType::Integer:
            if (raw->kind == Kind::Integer || raw->kind == Kind::LslInteger)
                return unbox_lsl_value(*value);
            break;
        case YEngineReferenceType::Float:
            if (raw->kind == Kind::Double || raw->kind == Kind::LslFloat)
                return unbox_lsl_value(*value);
            if (raw->kind == Kind::Float)
                return static_cast<double>(std::get<LslRawSingle>(*value).value);
            break;
        case YEngineReferenceType::Character:
            if (raw->kind == Kind::Character)
                return value;
            break;
        case YEngineReferenceType::Boolean:
            if (raw->kind == Kind::Boolean)
                return value;
            break;
        case YEngineReferenceType::Vector:
            if (raw->kind == Kind::LslVector)
                return value;
            break;
        case YEngineReferenceType::Rotation:
            if (raw->kind == Kind::LslRotation)
                return value;
            break;
        default:
            break;
        }
        return {};
    }
};

YEngineReferenceProjection::YEngineReferenceProjection() : state_(std::make_shared<State>()) {}

YEngineReferenceProjection::YEngineReferenceProjection(YEngineScriptClassProjectionContext context)
    : state_(std::make_shared<State>()) {
    state_->class_context = std::move(context);
}

std::optional<LslValue> YEngineReferenceProjection::project(const YEngineManagedRef &raw, YEngineReferenceType type) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    if (state_->failed)
        return {};
    try {
        auto result = state_->project(raw, type);
        if (!result)
            state_->failed = true;
        return result;
    } catch (const std::exception &) {
        state_->failed = true;
        return {};
    }
}
} // namespace opensim::script

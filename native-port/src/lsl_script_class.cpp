// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_script_class.hpp"
#include "lsl_array_conversion_support.hpp"
#include "opensim/script/lsl_reference_string.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <set>

namespace opensim::script {
namespace {
std::int32_t signed_bits(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

std::int32_t add(std::int32_t a, std::int32_t b) {
    return signed_bits(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

std::int32_t multiply(std::int32_t a, std::int32_t b) {
    return signed_bits(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

std::int32_t object_size(const LslValue &value, bool list_entry) {
    if (auto box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value)) {
        if (!*box || !valid_lsl_object_box(**box))
            throw std::invalid_argument("Invalid CLR Object box");
        return object_size((*box)->value, list_entry);
    }
    if (std::holds_alternative<LslNullObject>(value) || std::holds_alternative<LslNullList>(value) ||
        std::holds_alternative<LslNullString>(value)) {
        if (list_entry)
            throw std::runtime_error("Exception: null entry in List.Size");
        return 0;
    }
    if (std::holds_alternative<LslBoxedNullString>(value)) {
        if (list_entry)
            return 0;
        throw std::runtime_error("NullReferenceException: Object.Size SDK string field");
    }
    if (std::holds_alternative<LslBoxedKey>(value) || std::holds_alternative<LslBoxedUtf16Key>(value) ||
        std::holds_alternative<LslBoxedNullKey>(value)) {
        // LSL_Types.key is distinct from the LSLString alias used by YEngine's key variables.
        if (!list_entry)
            throw std::runtime_error("Exception: unknown size of type key");
        const auto text = lsl_string_code_units(value);
        if (!text)
            throw std::runtime_error("NullReferenceException: List.Size key field");
        return signed_bits(static_cast<std::uint32_t>(text->size()));
    }
    if (std::holds_alternative<LslRawBoolean>(value) || std::holds_alternative<LslRawUuid>(value))
        throw std::runtime_error(list_entry ? "Exception: Unknown type in List.Size"
                                            : "Exception: unknown size of type");
    if (std::holds_alternative<std::int32_t>(value) || std::holds_alternative<LslBoxedInteger>(value) ||
        std::holds_alternative<LslRawSingle>(value))
        return 4;
    if (std::holds_alternative<double>(value) || std::holds_alternative<LslBoxedFloat>(value))
        return 8;
    if (std::holds_alternative<framework::Vector3d>(value))
        return 24;
    if (std::holds_alternative<LslRotation>(value))
        return 32;
    if (std::holds_alternative<LslCharacter>(value)) {
        if (list_entry)
            throw std::runtime_error("Exception: Unknown type in List.Size: System.Char");
        return 2;
    }
    if (auto text = lsl_string_code_units(value))
        return multiply(signed_bits(static_cast<std::uint32_t>(text->size())), 2);
    if (auto list = std::get_if<std::shared_ptr<const LslList>>(&value)) {
        if (!*list) {
            if (list_entry)
                throw std::runtime_error("Exception: null entry in List.Size");
            return 0;
        }
        static thread_local std::set<const LslList *> active;
        if (!active.insert(list->get()).second)
            throw std::runtime_error("Recursive List.Size is unsafe (original StackOverflowException)");

        struct Release {
            const LslList *ptr;

            ~Release() {
                active.erase(ptr);
            }
        } release{list->get()};

        auto bytes = signed_bits(static_cast<std::uint32_t>((*list)->items.size() * sizeof(void *)));
        for (const auto &item : (*list)->items)
            bytes = add(bytes, object_size(item, true));
        return bytes;
    }
    if (auto instance = std::get_if<std::shared_ptr<const LslScriptClass>>(&value)) {
        if (list_entry && *instance)
            throw std::runtime_error("Exception: Unknown type in List.Size: XMRSDTypeClObj");
        if (list_entry)
            throw std::runtime_error("Exception: null entry in List.Size");
        return 0;
    }
    if (auto array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value)) {
        if (!*array) {
            if (list_entry)
                throw std::runtime_error("Exception: null entry in List.Size");
            return 0;
        }
        if (list_entry)
            throw std::runtime_error("Exception: Unknown type in List.Size: System.Array");
        const auto length = lsl_fixed_array_size(*array);
        if ((*array)->kind != LslFixedArrayKind::Object)
            return multiply(signed_bits(static_cast<std::uint32_t>(length)),
                            (*array)->kind == LslFixedArrayKind::Character ? 2
                            : (*array)->kind == LslFixedArrayKind::Float   ? 8
                                                                           : 4);
        static thread_local std::set<const LslFixedArray *> active;
        if (!active.insert(array->get()).second)
            throw std::runtime_error("Recursive Object.Size is unsafe (original StackOverflowException)");

        struct Release {
            const LslFixedArray *ptr;

            ~Release() {
                active.erase(ptr);
            }
        } release{array->get()};

        std::int32_t size = 0;
        for (const auto &cell : std::get<std::vector<LslValue>>((*array)->cells))
            size = add(size, object_size(cell, false));
        return size;
    }
    throw std::runtime_error("Exception: unknown size of type");
}

const LslScriptClass &require_class(const std::shared_ptr<const LslScriptClass> &value) {
    if (!value)
        throw std::runtime_error("NullReferenceException: script class");
    if (!value->descriptor)
        throw std::invalid_argument("Missing script class descriptor");
    return *value;
}

const LslScriptClass &require_array(const std::shared_ptr<const LslScriptClass> &value) {
    const auto &instance = require_class(value);
    const auto rank = instance.descriptor->array_rank;
    if (!rank || instance.instance_arrays[3].size() != rank || instance.instance_arrays[5].size() != 1)
        throw std::invalid_argument("Invalid generated array instance");
    return instance;
}

LslFixedArrayKind backing_kind(const LslScriptClassDescriptor &descriptor) {
    if (descriptor.element_type == "char")
        return LslFixedArrayKind::Character;
    if (descriptor.element_type == "float")
        return LslFixedArrayKind::Float;
    if (descriptor.element_type == "integer")
        return LslFixedArrayKind::Integer;
    return LslFixedArrayKind::Object;
}

std::string array_clr_name(LslFixedArrayKind kind) {
    switch (kind) {
    case LslFixedArrayKind::Character:
        return "System.Char[]";
    case LslFixedArrayKind::Float:
        return "System.Double[]";
    case LslFixedArrayKind::Integer:
        return "System.Int32[]";
    case LslFixedArrayKind::Object:
        return "System.Object[]";
    }
    throw std::invalid_argument("Invalid fixed array kind");
}

std::string object_clr_name(const LslValue &value) {
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value))
        return object_clr_name((*box)->value);
    if (std::holds_alternative<LslBoxedString>(value) || std::holds_alternative<LslBoxedUtf16String>(value) ||
        std::holds_alternative<LslBoxedNullString>(value))
        return "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString";
    if (std::holds_alternative<LslBoxedKey>(value) || std::holds_alternative<LslBoxedUtf16Key>(value) ||
        std::holds_alternative<LslBoxedNullKey>(value))
        return "OpenSim.Region.ScriptEngine.Shared.LSL_Types+key";
    if (lsl_string_code_units(value))
        return "System.String";
    if (const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value))
        return array_clr_name((*array)->kind);
    if (std::holds_alternative<std::shared_ptr<const LslScriptClass>>(value))
        return "OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj";
    if (std::holds_alternative<std::int32_t>(value))
        return "System.Int32";
    if (std::holds_alternative<double>(value))
        return "System.Double";
    if (std::holds_alternative<LslCharacter>(value))
        return "System.Char";
    if (std::holds_alternative<LslRawSingle>(value))
        return "System.Single";
    if (std::holds_alternative<LslRawBoolean>(value))
        return "System.Boolean";
    if (std::holds_alternative<LslRawUuid>(value))
        return "OpenMetaverse.UUID";
    constexpr auto sdk = "OpenSim.Region.ScriptEngine.Shared.LSL_Types+";
    if (std::holds_alternative<LslBoxedInteger>(value))
        return std::string(sdk) + "LSLInteger";
    if (std::holds_alternative<LslBoxedFloat>(value))
        return std::string(sdk) + "LSLFloat";
    if (std::holds_alternative<framework::Vector3d>(value))
        return std::string(sdk) + "Vector3";
    if (std::holds_alternative<LslRotation>(value))
        return std::string(sdk) + "Quaternion";
    if (std::holds_alternative<std::shared_ptr<const LslList>>(value))
        return std::string(sdk) + "list";
    throw std::invalid_argument("Unknown generated array Object backing type");
}

std::shared_ptr<const LslFixedArray> backing(const LslScriptClass &instance) {
    const auto &value = instance.instance_arrays[5].at(0);
    if (std::holds_alternative<LslNullObject>(value))
        throw std::runtime_error("NullReferenceException: generated array backing");
    const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value);
    if (!array)
        throw std::runtime_error("InvalidCastException: Unable to cast object of type '" + object_clr_name(value) +
                                 "' to type '" + array_clr_name(backing_kind(*instance.descriptor)) + "'.");
    if (!*array)
        throw std::runtime_error("NullReferenceException: generated array backing");
    if ((*array)->kind != backing_kind(*instance.descriptor))
        throw std::runtime_error("InvalidCastException: Unable to cast object of type '" +
                                 array_clr_name((*array)->kind) + "' to type '" +
                                 array_clr_name(backing_kind(*instance.descriptor)) + "'.");
    return *array;
}

LslValue default_field(std::size_t lane) {
    switch (lane) {
    case 1:
        return LslCharacter{0};
    case 2:
        return 0.0;
    case 3:
        return std::int32_t{0};
    case 4:
        return LslNullList{};
    case 6:
        return LslRotation{0, 0, 0, 0};
    case 7:
        return LslNullString{};
    case 8:
        return framework::Vector3d{};
    default:
        return LslNullObject{};
    }
}

std::int32_t base_size(const LslScriptClassDescriptor &descriptor) {
    constexpr std::array<std::int32_t, 11> sizes{0, 2, 8, 4, 0, 0, 32, 0, 24, 0, 8};
    std::int32_t bytes = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i)
        bytes = add(bytes, multiply(signed_bits(descriptor.instance_sizes[i]), sizes[i]));
    return bytes;
}
} // namespace

std::string array_conversion_detail::object_type_name(const LslValue &value) {
    return object_clr_name(value);
}

bool supported_lsl_primitive_array_descriptor(std::string_view name, std::string_view element, std::uint32_t rank) {
    if (!rank || rank > name.size())
        return false;
    const auto first = element.find('[');
    const auto base = element.substr(0, first);
    if (base != "integer" && base != "float" && base != "char")
        return false;
    auto position = first == std::string_view::npos ? element.size() : first;
    while (position < element.size()) {
        if (element[position++] != '[')
            return false;
        while (position < element.size() && element[position] == ',')
            ++position;
        if (position == element.size() || element[position++] != ']')
            return false;
    }
    std::string expected(element);
    expected.insert(first == std::string_view::npos ? element.size() : first, "[" + std::string(rank - 1, ',') + "]");
    return expected == name;
}

std::shared_ptr<const LslScriptClassDescriptor>
make_lsl_script_array_descriptor(std::string source_hash, std::string element_type, std::uint32_t rank) {
    if (source_hash.empty() || element_type.empty() || rank == 0)
        throw std::invalid_argument("Invalid generated array type");
    auto descriptor = std::make_shared<LslScriptClassDescriptor>();
    descriptor->source_hash = std::move(source_hash);
    descriptor->element_type = std::move(element_type);
    descriptor->canonical_name = descriptor->element_type;
    auto insertion = descriptor->canonical_name.find('[');
    if (insertion == std::string::npos)
        insertion = descriptor->canonical_name.size();
    descriptor->canonical_name.insert(insertion, "[" + std::string(rank - 1, ',') + "]");
    descriptor->array_rank = rank;
    descriptor->instance_sizes[3] = rank;
    descriptor->instance_sizes[5] = 1;
    return descriptor;
}

std::shared_ptr<const LslStringReference>
lsl_script_class_type_name(const std::shared_ptr<const LslScriptClassDescriptor> &descriptor) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    if (!descriptor)
        throw std::invalid_argument("Missing script class descriptor");
    if (!descriptor->type_name_reference) {
        if (descriptor->type_name_owner.is_zero())
            descriptor->type_name_owner = framework::Uuid::random();
        descriptor->type_name_reference = lsl_class_type_name_cache(
            {descriptor->source_hash, descriptor->canonical_name, descriptor->type_name_owner});
    }
    return descriptor->type_name_reference;
}

bool same_lsl_script_class_type(const LslScriptClassDescriptor &a, const LslScriptClassDescriptor &b) {
    return a.source_hash == b.source_hash && a.canonical_name == b.canonical_name && a.element_type == b.element_type &&
           a.array_rank == b.array_rank && a.instance_sizes == b.instance_sizes;
}

LslValue make_lsl_script_class(std::shared_ptr<const LslScriptClassDescriptor> descriptor) {
    if (!descriptor)
        throw std::invalid_argument("Missing script class descriptor");
    auto value = std::make_shared<LslScriptClass>();
    value->descriptor = std::move(descriptor);
    for (std::size_t lane = 0; lane < value->instance_arrays.size(); ++lane)
        value->instance_arrays[lane].assign(value->descriptor->instance_sizes[lane], default_field(lane));
    value->arrays_heap_usage = base_size(*value->descriptor);
    register_lsl_script_class(value);
    return std::shared_ptr<const LslScriptClass>(value);
}

LslValue make_lsl_script_array(std::shared_ptr<const LslScriptClassDescriptor> descriptor,
                               std::span<const std::int32_t> dimensions, const LslScriptClassHeapHooks &hooks) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    if (!descriptor || descriptor->array_rank != dimensions.size() || dimensions.empty())
        throw std::invalid_argument("Generated array dimension count mismatch");
    if (hooks.check_delta)
        hooks.check_delta(base_size(*descriptor));
    auto result = make_lsl_script_class(std::move(descriptor));
    const auto &value = std::get<std::shared_ptr<const LslScriptClass>>(result);
    require_array(value);
    std::int32_t length = 1;
    for (std::size_t i = 0; i < dimensions.size(); ++i) {
        value->instance_arrays[3][i] = dimensions[i];
        length = multiply(length, dimensions[i]);
    }
    if (length < 0)
        throw std::runtime_error("OverflowException: negative fixed array length");
    lsl_script_class_store_field(
        value, 5, 0, make_lsl_fixed_array(backing_kind(*value->descriptor), static_cast<std::size_t>(length)), hooks);
    return result;
}

std::int32_t lsl_script_class_object_size(const LslValue &value) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    return object_size(value, false);
}

LslValue lsl_script_class_field(const std::shared_ptr<const LslScriptClass> &value, std::size_t lane,
                                std::size_t index) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    return require_class(value).instance_arrays.at(lane).at(index);
}

void lsl_script_class_store_field(const std::shared_ptr<const LslScriptClass> &value, std::size_t lane,
                                  std::size_t index, LslValue replacement, const LslScriptClassHeapHooks &hooks) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto &instance = require_class(value);
    auto &field = instance.instance_arrays.at(lane).at(index);
    if (lane == 4 || lane == 5 || lane == 7) {
        auto size = [&](const LslValue &entry) {
            // String fields use HeapTrackerString, unlike Object's string sizing.
            if (lane == 7) {
                auto text = lsl_string_code_units(entry);
                if (!text || std::all_of(text->begin(), text->end(), framework::unicode_whitespace))
                    return 0;
                return multiply(signed_bits(static_cast<std::uint32_t>(text->size())), 2);
            }
            return hooks.size_of ? hooks.size_of(entry) : object_size(entry, false);
        };
        const auto new_size = size(replacement);
        const auto old_size = size(field);
        const auto delta = signed_bits(static_cast<std::uint32_t>(new_size) - static_cast<std::uint32_t>(old_size));
        if (hooks.check_delta)
            hooks.check_delta(delta);
        instance.arrays_heap_usage = add(instance.arrays_heap_usage, delta);
    }
    field = std::move(replacement);
}

std::int32_t lsl_script_array_length(const std::shared_ptr<const LslScriptClass> &value) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto &instance = require_array(value);
    std::int32_t length = 1;
    for (const auto &dimension : instance.instance_arrays[3])
        length = multiply(length, std::get<std::int32_t>(dimension));
    return length;
}

std::int32_t lsl_script_array_length(const std::shared_ptr<const LslScriptClass> &value, std::int32_t dimension) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto &instance = require_array(value);
    if (dimension < 0 || static_cast<std::uint32_t>(dimension) >= instance.descriptor->array_rank)
        return 0;
    return std::get<std::int32_t>(instance.instance_arrays[3][static_cast<std::size_t>(dimension)]);
}

std::int32_t lsl_script_array_index(const std::shared_ptr<const LslScriptClass> &value,
                                    std::span<const std::int32_t> indices) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto &instance = require_array(value);
    if (indices.size() != instance.descriptor->array_rank || indices.empty())
        throw std::invalid_argument("Generated array index count mismatch");
    auto index = indices[0];
    for (std::size_t i = 1; i < indices.size(); ++i)
        index = add(multiply(index, std::get<std::int32_t>(instance.instance_arrays[3][i])), indices[i]);
    return index;
}

LslValue lsl_script_array_get(const std::shared_ptr<const LslScriptClass> &value,
                              std::span<const std::int32_t> indices) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto index = lsl_script_array_index(value, indices);
    return lsl_fixed_array_get(backing(require_array(value)), static_cast<std::size_t>(index));
}

void lsl_script_array_set(const std::shared_ptr<const LslScriptClass> &value, std::span<const std::int32_t> indices,
                          const LslValue &replacement) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto index = lsl_script_array_index(value, indices);
    lsl_fixed_array_set(backing(require_array(value)), static_cast<std::size_t>(index), replacement);
}

namespace {
std::shared_ptr<const LslFixedArray> conversion_character_backing(const LslValue &value) {
    if (std::holds_alternative<LslNullObject>(value))
        throw std::runtime_error("NullReferenceException: Object reference not set to an instance of an object.");
    const auto *wrapper = std::get_if<std::shared_ptr<const LslScriptClass>>(&value);
    if (!wrapper || !*wrapper)
        throw std::runtime_error("InvalidCastException: Unable to cast object of type '" + object_clr_name(value) +
                                 "' to type 'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'.");
    const auto &instance = **wrapper;
    if (!instance.descriptor || !instance.descriptor->array_rank)
        throw std::runtime_error("InvalidCastException: only do arrays not " +
                                 (instance.descriptor ? instance.descriptor->canonical_name : std::string{}));
    const auto &object = instance.instance_arrays[5].at(0);
    if (std::holds_alternative<LslNullObject>(object))
        return {};
    const auto *backing = std::get_if<std::shared_ptr<const LslFixedArray>>(&object);
    if (!backing || !*backing || (*backing)->kind != LslFixedArrayKind::Character)
        throw std::runtime_error("InvalidCastException: Unable to cast object of type '" + object_clr_name(object) +
                                 "' to type 'System.Char[]'.");
    return *backing;
}

std::string conversion_newline() {
#ifdef _WIN32
    return "\r\n";
#else
    return "\n";
#endif
}

[[noreturn]] void character_range_error(std::string_view parameter, std::int32_t value,
                                        std::optional<std::int64_t> maximum = {}) {
    auto message =
        "ArgumentOutOfRangeException: " + std::string(parameter) + " ('" + std::to_string(value) + "') must be ";
    message += maximum ? "less than or equal to '" + std::to_string(*maximum) + "'." : "a non-negative value.";
    message += " (Parameter '" + std::string(parameter) + "')" + conversion_newline() + "Actual value was " +
               std::to_string(value) + ".";
    throw std::runtime_error(message);
}
} // namespace

LslValue lsl_chars_to_string(const LslValue &value, std::int32_t start, std::int32_t count) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto source = conversion_character_backing(value);
    if (!source)
        throw std::runtime_error("ArgumentNullException: Value cannot be null. (Parameter 'value')");
    if (start < 0)
        character_range_error("startIndex", start);
    if (count < 0)
        character_range_error("length", count);
    const auto maximum = static_cast<std::int64_t>(lsl_fixed_array_size(source)) - count;
    if (start > maximum)
        character_range_error("startIndex", start, maximum);
    std::u16string text;
    text.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i)
        text.push_back(std::get<LslCharacter>(lsl_fixed_array_get(source, static_cast<std::size_t>(start) + i)).value);
    return make_lsl_reference_string(text, text.empty());
}

void lsl_string_to_chars(const LslValue &source, std::int32_t source_start, const LslValue &destination,
                         std::int32_t destination_start, std::int32_t count) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    // The original casts the destination backing before checking count. A null
    // backing is legal for zero count; a wrong backing family still faults.
    const auto target = conversion_character_backing(destination);
    if (count <= 0)
        return;
    const auto text = lsl_string_code_units(source);
    if (!text)
        throw std::runtime_error("NullReferenceException: Object reference not set to an instance of an object.");
    for (std::int32_t i = 0; i < count; ++i) {
        const auto from = signed_bits(static_cast<std::uint32_t>(source_start) + static_cast<std::uint32_t>(i));
        if (from < 0 || static_cast<std::size_t>(from) >= text->size())
            throw std::runtime_error("IndexOutOfRangeException: Index was outside the bounds of the array.");
        if (!target)
            throw std::runtime_error("NullReferenceException: Object reference not set to an instance of an object.");
        const auto to = signed_bits(static_cast<std::uint32_t>(destination_start) + static_cast<std::uint32_t>(i));
        if (to < 0 || static_cast<std::size_t>(to) >= lsl_fixed_array_size(target))
            throw std::runtime_error("IndexOutOfRangeException: Index was outside the bounds of the array.");
        lsl_fixed_array_set(target, static_cast<std::size_t>(to), LslCharacter{(*text)[from]});
    }
}
} // namespace opensim::script

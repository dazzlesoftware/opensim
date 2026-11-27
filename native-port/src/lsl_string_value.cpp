// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/lsl_reference_string.hpp"
#include "opensim/framework/text.hpp"
#include <map>
#include <mutex>
#include <array>
#include <tuple>

namespace opensim::script {
namespace {
bool well_formed(std::u16string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] >= 0xd800 && text[i] <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                return false;
        } else if (text[i] >= 0xdc00 && text[i] <= 0xdfff)
            return false;
    }
    return true;
}

struct LiteralPool {
    std::mutex mutex;
    std::map<std::u16string, std::shared_ptr<const LslStringReference>> values;
};

LiteralPool &literal_pool() {
    static LiteralPool pool;
    return pool;
}
} // namespace

LslValue make_lsl_class_type_name_string(const LslClassTypeNameOrigin &origin) {
    if (origin.owner.is_zero() || origin.source_hash.empty() || origin.canonical_name.empty())
        throw std::invalid_argument("Invalid class type-name cache origin");
    return std::make_shared<const LslStringReference>(LslStringReference{
        framework::utf16_replacing(origin.canonical_name), std::make_shared<const LslClassTypeNameOrigin>(origin)});
}

std::optional<LslClassTypeNameOrigin>
lsl_reference_string_class_type_name(const std::shared_ptr<const LslStringReference> &value) {
    if (!value || !value->class_type_name_origin)
        return {};
    const auto &origin = *value->class_type_name_origin;
    if (origin.owner.is_zero() || origin.source_hash.empty() || origin.canonical_name.empty() ||
        value->units != framework::utf16_replacing(origin.canonical_name))
        throw std::invalid_argument("Invalid class type-name cache origin");
    return origin;
}

std::shared_ptr<const LslStringReference>
lsl_class_type_name_cache(const LslClassTypeNameOrigin &origin, std::shared_ptr<const LslStringReference> preferred) {
    if (origin.owner.is_zero() || origin.source_hash.empty() || origin.canonical_name.empty())
        throw std::invalid_argument("Invalid class type-name cache origin");
    if (preferred && lsl_reference_string_class_type_name(preferred) != origin)
        throw std::invalid_argument("Class type-name cache origin mismatch");

    struct Cache {
        std::mutex mutex;
        std::map<std::tuple<framework::Uuid, std::string, std::string>, std::weak_ptr<const LslStringReference>> values;
        unsigned inserted = 0;
    };

    static Cache cache;
    std::lock_guard lock(cache.mutex);
    const auto key = std::tuple{origin.owner, origin.source_hash, origin.canonical_name};
    if (const auto found = cache.values.find(key); found != cache.values.end())
        if (auto existing = found->second.lock())
            return existing;
    // The registry must not keep unloaded object codes or their names alive.
    // Amortize removal rather than retaining an unbounded set of dead owners.
    if ((cache.inserted++ & 255u) == 0)
        std::erase_if(cache.values, [](const auto &entry) { return entry.second.expired(); });
    if (!preferred)
        preferred = std::get<std::shared_ptr<const LslStringReference>>(make_lsl_class_type_name_string(origin));
    cache.values.insert_or_assign(key, preferred);
    return preferred;
}

bool is_null_lsl_string(const LslValue &value) {
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value))
        return *box && (std::holds_alternative<LslBoxedNullString>((*box)->value) ||
                        std::holds_alternative<LslBoxedNullKey>((*box)->value));
    return std::holds_alternative<LslNullString>(value) || std::holds_alternative<LslBoxedNullString>(value) ||
           std::holds_alternative<LslBoxedNullKey>(value);
}

std::optional<std::u16string> lsl_string_code_units(const LslValue &value) {
    if (const auto *reference = std::get_if<std::shared_ptr<const LslStringReference>>(&value))
        return *reference ? std::optional<std::u16string>((*reference)->units) : std::nullopt;
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value)) {
        if (*box && (*box)->string_field &&
            (std::holds_alternative<LslBoxedString>((*box)->value) ||
             std::holds_alternative<LslBoxedKey>((*box)->value) ||
             std::holds_alternative<LslBoxedUtf16String>((*box)->value) ||
             std::holds_alternative<LslBoxedUtf16Key>((*box)->value)))
            return (*box)->string_field->units;
        return std::nullopt;
    }
    if (const auto *text = std::get_if<std::string>(&value))
        return framework::utf16_replacing(*text);
    if (const auto *text = std::get_if<LslBoxedString>(&value))
        return framework::utf16_replacing(text->value);
    if (const auto *text = std::get_if<LslBoxedKey>(&value))
        return framework::utf16_replacing(text->value);
    if (const auto *text = std::get_if<LslUtf16String>(&value))
        return text->value;
    if (const auto *text = std::get_if<LslBoxedUtf16String>(&value))
        return text->value;
    if (const auto *text = std::get_if<LslBoxedUtf16Key>(&value))
        return text->value;
    return std::nullopt;
}

LslValue make_lsl_reference_string(std::u16string_view units, bool literal) {
    if (!literal)
        return std::make_shared<const LslStringReference>(LslStringReference{std::u16string(units)});
    auto &pool = literal_pool();
    std::lock_guard lock(pool.mutex);
    auto [entry, inserted] = pool.values.try_emplace(std::u16string(units));
    if (inserted)
        entry->second = std::make_shared<const LslStringReference>(LslStringReference{entry->first});
    return entry->second;
}

bool lsl_reference_string_is_literal(const std::shared_ptr<const LslStringReference> &value) {
    if (!value)
        return false;
    auto &pool = literal_pool();
    std::lock_guard lock(pool.mutex);
    const auto found = pool.values.find(value->units);
    return found != pool.values.end() && found->second == value;
}

namespace {
const auto &integer_strings() {
    static const auto values = [] {
        std::array<std::shared_ptr<const LslStringReference>, 300> result;
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = std::make_shared<const LslStringReference>(
                LslStringReference{framework::utf16_replacing(std::to_string(i))});
        return result;
    }();
    return values;
}
} // namespace

LslValue make_lsl_cached_integer_string(std::int32_t value) {
    if (value >= 0 && value < 300)
        return integer_strings()[value];
    return make_lsl_reference_string(framework::utf16_replacing(std::to_string(value)));
}

std::optional<std::int32_t>
lsl_reference_string_cached_integer(const std::shared_ptr<const LslStringReference> &value) {
    if (value)
        for (std::int32_t i = 0; i < 300; ++i)
            if (integer_strings()[i] == value && value->units == framework::utf16_replacing(std::to_string(i)))
                return i;
    return std::nullopt;
}

bool valid_lsl_object_box(const LslObjectBox &box) {
    const bool text =
        std::holds_alternative<LslBoxedString>(box.value) || std::holds_alternative<LslBoxedKey>(box.value) ||
        std::holds_alternative<LslBoxedUtf16String>(box.value) || std::holds_alternative<LslBoxedUtf16Key>(box.value);
    const bool null_text =
        std::holds_alternative<LslBoxedNullString>(box.value) || std::holds_alternative<LslBoxedNullKey>(box.value);
    if (text)
        return box.string_field && lsl_string_code_units(box.value) == box.string_field->units;
    if (box.string_field)
        return false;
    if (null_text)
        return true;
    return std::holds_alternative<std::int32_t>(box.value) || std::holds_alternative<double>(box.value) ||
           std::holds_alternative<framework::Vector3d>(box.value) || std::holds_alternative<LslRotation>(box.value) ||
           std::holds_alternative<LslBoxedInteger>(box.value) || std::holds_alternative<LslBoxedFloat>(box.value) ||
           std::holds_alternative<LslCharacter>(box.value) || std::holds_alternative<LslRawSingle>(box.value) ||
           std::holds_alternative<LslRawBoolean>(box.value) || std::holds_alternative<LslRawUuid>(box.value);
}

namespace {
const auto &array_type_strings() {
    static const std::array<std::shared_ptr<const LslStringReference>, 4> values{
        std::make_shared<const LslStringReference>(LslStringReference{u"System.Char[]"}),
        std::make_shared<const LslStringReference>(LslStringReference{u"System.Double[]"}),
        std::make_shared<const LslStringReference>(LslStringReference{u"System.Int32[]"}),
        std::make_shared<const LslStringReference>(LslStringReference{u"System.Object[]"})};
    return values;
}
} // namespace

LslValue make_lsl_cached_array_type_string(std::uint8_t kind) {
    if (kind >= 4)
        throw std::invalid_argument("Invalid cached array type");
    return array_type_strings()[kind];
}

std::optional<std::uint8_t>
lsl_reference_string_cached_array_type(const std::shared_ptr<const LslStringReference> &value) {
    for (std::uint8_t i = 0; i < 4; ++i)
        if (value && array_type_strings()[i] == value && value->units == array_type_strings()[i]->units)
            return i;
    return std::nullopt;
}

LslValue make_lsl_object_box(LslValue value, std::shared_ptr<const LslStringReference> field) {
    if (!field &&
        (std::holds_alternative<LslBoxedString>(value) || std::holds_alternative<LslBoxedKey>(value) ||
         std::holds_alternative<LslBoxedUtf16String>(value) || std::holds_alternative<LslBoxedUtf16Key>(value)))
        field = std::get<std::shared_ptr<const LslStringReference>>(
            make_lsl_reference_string(*lsl_string_code_units(value), lsl_string_code_units(value)->empty()));
    auto box = std::make_shared<const LslObjectBox>(LslObjectBox{std::move(value), std::move(field)});
    if (!valid_lsl_object_box(*box))
        throw std::invalid_argument("Invalid CLR Object box payload or string field");
    return box;
}

LslValue make_lsl_string(std::u16string_view text) {
    if (well_formed(text))
        return framework::utf8(text);
    return LslUtf16String{std::u16string(text)};
}

LslValue box_lsl_string(std::u16string_view text, bool key) {
    if (well_formed(text)) {
        auto encoded = framework::utf8(text);
        if (key)
            return LslBoxedKey{std::move(encoded)};
        return LslBoxedString{std::move(encoded)};
    }
    if (key)
        return LslBoxedUtf16Key{std::u16string(text)};
    return LslBoxedUtf16String{std::u16string(text)};
}
} // namespace opensim::script

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/yengine_xml_values.hpp"
#include "opensim/script/lsl_fixed_array.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/framework/invariant_number_format.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/nini_xml_encoder.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/vector_text.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace opensim::script {
namespace {
constexpr std::string_view script_class_name = "OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj";
constexpr std::string_view sdk_prefix = "OpenSim.Region.ScriptEngine.Shared.LSL_Types+";

std::u16string_view trim(std::u16string_view value, bool nul = false) {
    const auto padding = [nul](char16_t c) { return framework::unicode_whitespace(c) || (nul && c == 0); };
    while (!value.empty() && padding(value.front()))
        value.remove_prefix(1);
    while (!value.empty() && padding(value.back()))
        value.remove_suffix(1);
    return value;
}

bool equals_ascii(std::u16string_view value, std::u16string_view lower) {
    if (value.size() != lower.size())
        return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = value[i];
        if (c >= u'A' && c <= u'Z')
            c += u'a' - u'A';
        if (c != lower[i])
            return false;
    }
    return true;
}

bool sdk_number(std::u16string_view text, double &value, bool invariant_symbols) {
    // SDK vector/rotation constructors use NumberStyles.Float and en-US,
    // whereas SDK scalar floats use the invariant NumberFormatInfo.
    if (text.find(u',') != text.npos)
        return false;
    const auto word = trim(text);
    if (!invariant_symbols &&
        (equals_ascii(word, u"infinity") || equals_ascii(word, u"+infinity") || equals_ascii(word, u"-infinity")))
        return false;
    if (!invariant_symbols && (word == u"\u221e" || word == u"+\u221e" || word == u"-\u221e")) {
        value =
            word.front() == u'-' ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        return true;
    }
    try {
        value = framework::invariant_convert_to_double(std::u16string(text));
        return true;
    } catch (const framework::InvariantNumberError &) {
        return false;
    }
}

LslBoxedInteger sdk_integer(std::u16string_view text) {
    while (!text.empty() && text.front() == u' ')
        text.remove_prefix(1);
    if (text.starts_with(u"0x") || text.starts_with(u"0X")) {
        std::uint32_t value = 0;
        for (const auto c : text.substr(2)) {
            unsigned digit;
            if (c >= u'0' && c <= u'9')
                digit = c - u'0';
            else if (c >= u'a' && c <= u'f')
                digit = c - u'a' + 10;
            else if (c >= u'A' && c <= u'F')
                digit = c - u'A' + 10;
            else
                break;
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 16)
                return {-1};
            value = value * 16 + digit;
        }
        return {std::bit_cast<std::int32_t>(value)};
    }
    bool negative = false;
    if (!text.empty() && (text.front() == u'-' || text.front() == u'+')) {
        negative = text.front() == u'-';
        text.remove_prefix(1);
    }
    std::int32_t value = 0;
    for (const auto c : text) {
        if (c < u'0' || c > u'9')
            break;
        const auto digit = static_cast<std::int32_t>(c - u'0');
        // The original constructor accumulates a checked positive Int32 even
        // for a negative input. Its overflow result is deliberately -1.
        if (value > (std::numeric_limits<std::int32_t>::max() - digit) / 10)
            return {-1};
        value = value * 10 + digit;
    }
    return {negative ? -value : value};
}

double sdk_float(std::u16string_view text) {
    double value = 0;
    if (sdk_number(text, value, true))
        return value;
    if (text.size() < 3)
        return 0;
    while (!text.empty() && framework::unicode_whitespace(text.front()))
        text.remove_prefix(1);
    if (text.empty())
        throw std::runtime_error("IndexOutOfRangeException: Index was outside the bounds of the array.");
    bool negative = false;
    if (!text.empty() && text.front() == u'-') {
        negative = true;
        text.remove_prefix(1);
        while (!text.empty() && framework::unicode_whitespace(text.front()))
            text.remove_prefix(1);
    }
    if (text.size() >= 3 && equals_ascii(text.substr(0, 3), u"nan"))
        return std::bit_cast<double>(std::uint64_t{0xfff8000000000000});
    if (text.size() >= 3 && equals_ascii(text.substr(0, 3), u"inf"))
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    return 0;
}

template <std::size_t Count> bool sdk_tuple(std::u16string_view text, std::array<double, Count> &values) {
    if (text.size() < Count * 2 - 1)
        return false;
    const auto first = text.find_first_of(u"<,");
    if (first == text.npos)
        return false;
    std::size_t start = first && text[first] != u'<' ? 0 : first + (text[first] == u'<');
    for (std::size_t i = 0; i < Count; ++i) {
        const auto delimiter = text.find(i + 1 == Count ? u'>' : u',', start);
        const auto end = delimiter == text.npos ? text.size() : delimiter;
        if ((i + 1 < Count && delimiter == text.npos) || !sdk_number(text.substr(start, end - start), values[i], false))
            return false;
        start = end + 1;
    }
    return true;
}

std::u16string scalar_text(const LslValue &value, std::string_view &type) {
    if (const auto *instance = std::get_if<std::shared_ptr<const LslScriptClass>>(&value)) {
        if (!*instance)
            throw std::invalid_argument("Invalid script class in queued XML");
        type = script_class_name;
        return framework::utf16(type);
    }
    if (const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value)) {
        if (!*array)
            throw std::invalid_argument("Invalid fixed array in queued XML");
        static constexpr std::array<std::string_view, 4> names{"System.Char[]", "System.Double[]", "System.Int32[]",
                                                               "System.Object[]"};
        type = names.at(static_cast<std::size_t>((*array)->kind));
        return framework::utf16(type);
    }
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value)) {
        if (!*box || !valid_lsl_object_box(**box))
            throw std::invalid_argument("Invalid CLR Object box in queued XML");
        // CLR reflection observes the boxed payload's actual type. A boxed
        // SDK String is still an SDK String, even though its field is a CLR
        // String reference shared with another script value.
        return scalar_text((*box)->value, type);
    }
    if (const auto *reference = std::get_if<std::shared_ptr<const LslStringReference>>(&value);
        reference && !*reference)
        throw std::invalid_argument("Invalid CLR String reference in queued XML");
    if (std::holds_alternative<LslNullString>(value) || std::holds_alternative<LslNullList>(value) ||
        std::holds_alternative<LslNullObject>(value))
        throw std::runtime_error("NullReferenceException: Object reference not set to an instance of an object.");
    if (std::holds_alternative<LslBoxedNullString>(value)) {
        type = "LSLString";
        return {};
    }
    if (std::holds_alternative<LslBoxedNullKey>(value)) {
        type = "key";
        return {};
    }
    if (const auto text = lsl_string_code_units(value)) {
        type = value.index() == 8 || value.index() == 12   ? "LSLString"
               : value.index() == 9 || value.index() == 13 ? "key"
                                                           : "System.String";
        return *text;
    }
    if (const auto number = std::get_if<std::int32_t>(&value)) {
        type = "System.Int32";
        return framework::format_invariant_integer_utf16(*number, u"G");
    }
    if (const auto number = std::get_if<LslBoxedInteger>(&value)) {
        type = "LSLInteger";
        return framework::format_invariant_integer_utf16(number->value, u"G");
    }
    if (const auto number = std::get_if<double>(&value)) {
        type = "System.Double";
        return framework::format_invariant_double_utf16(*number, u"G");
    }
    if (const auto number = std::get_if<LslBoxedFloat>(&value)) {
        type = "LSLFloat";
        return framework::format_invariant_double_utf16(number->value, u"0.000000");
    }
    if (const auto number = std::get_if<LslRawSingle>(&value)) {
        type = "System.Single";
        return yengine_xml_raw_single_text(number->value);
    }
    if (const auto boolean = std::get_if<LslRawBoolean>(&value)) {
        type = "System.Boolean";
        return boolean->value ? u"True" : u"False";
    }
    if (const auto uuid = std::get_if<LslRawUuid>(&value)) {
        type = "OpenMetaverse.UUID";
        return framework::utf16(uuid->value.str());
    }
    if (const auto character = std::get_if<LslCharacter>(&value)) {
        type = "System.Char";
        return {character->value};
    }
    std::u16string text = u"<";
    const auto component = [&](double number, bool first) {
        if (!first)
            text += u", ";
        text += framework::format_invariant_double_utf16(number, u"0.000000");
    };
    if (const auto vector = std::get_if<framework::Vector3d>(&value)) {
        type = "Vector3";
        component(vector->x, true);
        component(vector->y, false);
        component(vector->z, false);
    } else if (const auto rotation = std::get_if<LslRotation>(&value)) {
        type = "Quaternion";
        component(rotation->x, true);
        component(rotation->y, false);
        component(rotation->z, false);
        component(rotation->w, false);
    } else
        throw std::invalid_argument("Unsupported queued XML scalar");
    return text + u">";
}

class QueueCopy {
  public:
    LslValue value(const LslValue &original, unsigned depth = 0) {
        if (const auto list = std::get_if<std::shared_ptr<const LslList>>(&original)) {
            if (!*list)
                throw std::invalid_argument("Invalid nested LSL checkpoint list");
            if (depth == 64)
                throw std::invalid_argument("Invalid nested LSL checkpoint list");
            auto copy = std::make_shared<LslList>();
            copy->items.reserve((*list)->items.size());
            // XML has no list reference IDs. Each occurrence becomes a fresh
            // list, including aliases shared with globals or another event.
            for (const auto &item : (*list)->items)
                copy->items.push_back(value(item, depth + 1));
            return std::shared_ptr<const LslList>(std::move(copy));
        }
        std::string_view type;
        const auto text = scalar_text(original, type);
        framework::NativeXmlTextEncoder encoder([](auto, auto) {});
        try {
            encoder.write(text);
        } catch (const framework::XmlEncoderError &error) {
            throw std::runtime_error("ArgumentException: " + framework::utf8(error.message));
        }
        // Original XML writing records Array/Class.ToString successfully. Reload
        // subsequently fails Convert.ChangeType(String, Array/Class). Keep that
        // failure deferred to the persistent checkpoint reader; an internal
        // checkpoint still carries the complete mutable array graph.
        if (std::holds_alternative<std::shared_ptr<const LslFixedArray>>(original) ||
            std::holds_alternative<std::shared_ptr<const LslScriptClass>>(original))
            return original;
        auto result = parse_yengine_xml_scalar(type, text);
        if (std::holds_alternative<std::shared_ptr<const LslStringReference>>(original)) {
            const auto units = lsl_string_code_units(result);
            if (!units)
                throw std::invalid_argument("Queued CLR String did not restore as String");
            // XmlNode.InnerText returns String.Empty for an empty element;
            // nonempty text nodes are separate references on each reload.
            return make_lsl_reference_string(*units, units->empty());
        }
        if (std::holds_alternative<std::shared_ptr<const LslObjectBox>>(original))
            return make_lsl_object_box(std::move(result));
        return result;
    }
};
} // namespace

std::u16string yengine_xml_raw_single_text(float value) {
    if (std::isnan(value))
        return u"NaN";
    if (std::isinf(value))
        return std::signbit(value) ? u"-Infinity" : u"Infinity";
    return framework::utf16(framework::invariant_float_string(value));
}

LslValue parse_yengine_xml_scalar(std::string_view type, std::u16string_view text) {
    if (type.starts_with(sdk_prefix))
        type.remove_prefix(sdk_prefix.size());
    if (type == "System.String")
        return make_lsl_string(text);
    if (type == "LSLString" || type == "key")
        return box_lsl_string(text, type == "key");
    if (type == "LSLInteger")
        return sdk_integer(text);
    if (type == "LSLFloat")
        return LslBoxedFloat{sdk_float(text)};
    if (type == "System.Int32")
        return framework::invariant_convert_to_int32(std::u16string(text));
    if (type == "System.Double")
        return framework::invariant_convert_to_double(std::u16string(text));
    if (type == "System.Single")
        return LslRawSingle{framework::invariant_convert_to_single(std::u16string(text))};
    if (type == "System.Boolean") {
        const auto word = trim(text, true);
        if (equals_ascii(word, u"true"))
            return LslRawBoolean{true};
        if (equals_ascii(word, u"false"))
            return LslRawBoolean{false};
        throw std::runtime_error("FormatException: String '" + framework::utf8(text) +
                                 "' was not recognized as a valid Boolean.");
    }
    if (type == "System.Char") {
        if (text.size() != 1)
            throw std::runtime_error("FormatException: String must be exactly one character long.");
        return LslCharacter{text.front()};
    }
    if (type == "System.Char[]" || type == "System.Double[]" || type == "System.Int32[]" || type == "System.Object[]" ||
        type == script_class_name)
        throw std::runtime_error("InvalidCastException: Invalid cast from 'System.String' to '" + std::string(type) +
                                 "'.");
    if (type == "OpenMetaverse.UUID") {
        framework::Uuid value;
        framework::try_parse_sdk_uuid(framework::utf8(text), value);
        return LslRawUuid{value};
    }
    if (type == "Vector3") {
        std::array<double, 3> values{};
        if (!sdk_tuple(text, values))
            return framework::Vector3d{};
        return framework::Vector3d{values[0], values[1], values[2]};
    }
    if (type == "Quaternion") {
        std::array<double, 4> values{};
        if (!sdk_tuple(text, values))
            return LslRotation{0, 0, 0, 1};
        return LslRotation{values[0], values[1], values[2], values[3]};
    }
    // The original returns null when neither Type.GetType lookup resolves.
    // Known but unrepresented CLR types must not masquerade as that outcome.
    if (type.starts_with("System."))
        throw std::invalid_argument("Unsupported CLR queued XML type: " + std::string(type));
    return LslNullObject{};
}

std::vector<LslValue> normalize_yengine_xml_event_arguments(std::span<const LslValue> values) {
    QueueCopy copy;
    std::vector<LslValue> result;
    result.reserve(values.size());
    for (const auto &value : values)
        result.push_back(copy.value(value));
    return result;
}

std::vector<LslValue> reference_yengine_xml_event_arguments(std::span<const LslValue> values) {
    std::map<const LslList *, std::shared_ptr<const LslList>> lists;
    std::set<const LslList *> active;
    const auto promote = [&](auto &&self, const LslValue &value, unsigned depth) -> LslValue {
        if (depth >= 64)
            throw std::invalid_argument("Queued Object reference graph exceeds the transfer limit");
        if (const auto *instance = std::get_if<std::shared_ptr<const LslScriptClass>>(&value)) {
            if (!*instance)
                throw std::invalid_argument("Invalid queued script class reference");
            return value;
        }
        if (const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value)) {
            if (!*array)
                throw std::invalid_argument("Invalid queued fixed array reference");
            return value;
        }
        if (const auto *reference = std::get_if<std::shared_ptr<const LslStringReference>>(&value)) {
            if (!*reference)
                throw std::invalid_argument("Invalid queued CLR String reference");
            return value;
        }
        if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value)) {
            if (!*box || !valid_lsl_object_box(**box))
                throw std::invalid_argument("Invalid queued CLR Object box");
            return value;
        }
        if (std::holds_alternative<LslNullObject>(value) || std::holds_alternative<LslNullString>(value) ||
            std::holds_alternative<LslNullList>(value))
            return LslNullObject{};
        if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(&value)) {
            if (!*list || active.contains(list->get()))
                throw std::invalid_argument("Invalid or cyclic queued Object List");
            if (const auto found = lists.find(list->get()); found != lists.end())
                return found->second;
            active.insert(list->get());
            auto copy = std::make_shared<LslList>();
            copy->items.reserve((*list)->items.size());
            bool changed = false;
            for (const auto &item : (*list)->items) {
                auto promoted = self(self, item, depth + 1);
                changed |= promoted.index() != item.index();
                if (const auto *nested = std::get_if<std::shared_ptr<const LslList>>(&item))
                    changed |= std::get<std::shared_ptr<const LslList>>(promoted) != *nested;
                copy->items.push_back(std::move(promoted));
            }
            active.erase(list->get());
            std::shared_ptr<const LslList> result = changed ? std::shared_ptr<const LslList>(copy) : *list;
            lists.emplace(list->get(), result);
            return result;
        }
        if (std::holds_alternative<std::string>(value) || std::holds_alternative<LslUtf16String>(value)) {
            const auto units = *lsl_string_code_units(value);
            return make_lsl_reference_string(units, units.empty());
        }
        return make_lsl_object_box(value);
    };
    std::vector<LslValue> result;
    result.reserve(values.size());
    for (const auto &value : values)
        result.push_back(promote(promote, value, 0));
    return result;
}
} // namespace opensim::script

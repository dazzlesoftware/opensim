// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_json.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/sdk_json_reader.hpp"
#include "opensim/framework/text.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace opensim::network {
namespace {
std::string quote(std::u16string_view text) {
    std::string result = "\"";
    constexpr char digits[] = "0123456789ABCDEF";
    for (char16_t c : text) {
        switch (c) {
        case u'"':
            result += "\\\"";
            break;
        case u'\\':
            result += "\\\\";
            break;
        case u'\n':
            result += "\\n";
            break;
        case u'\r':
            result += "\\r";
            break;
        case u'\t':
            result += "\\t";
            break;
        case u'\b':
            result += "\\b";
            break;
        case u'\f':
            result += "\\f";
            break;
        default:
            if (c >= 32 && c <= 126)
                result += static_cast<char>(c);
            else {
                result += "\\u";
                for (int shift = 12; shift >= 0; shift -= 4)
                    result += digits[(c >> shift) & 15];
            }
        }
    }
    return result + '"';
}
bool omitted(const Value& input) {
    const auto& v = input.view();
    using K = Value::Kind;
    switch (v.kind) {
    case K::Undefined:
        return true;
    case K::Boolean:
        return !v.boolean;
    case K::Integer:
        return v.integer == 0;
    case K::Long:
        return v.long_value == 0;
    case K::Real:
        return v.real == 0;
    case K::String:
    case K::Uri:
        return osd_string(v).empty();
    case K::Uuid:
        return osd_uuid(v).is_zero();
    case K::Binary:
    case K::Array:
        return false;
    case K::Map:
        return v.map.empty();
    default:
        return false;
    }
}
std::string serialize(const Value& input, bool preserve, std::size_t depth) {
    const auto& v = input.view();
    if (depth > 512)
        throw std::invalid_argument("OSD JSON nesting exceeds native limit");
    using K = Value::Kind;
    switch (v.kind) {
    case K::NullReference:
        throw std::runtime_error("Object reference not set to an instance of an object.");
    case K::Undefined:
        return "null";
    case K::Boolean:
        return v.boolean ? "true" : "false";
    case K::Integer:
        return std::to_string(v.integer);
    case K::Long:
        return std::to_string(v.long_value);
    case K::Real: {
        auto s = std::isinf(v.real) ? (v.real < 0 ? std::string("-Infinity") : std::string("Infinity"))
                                    : osd_string(v);
        for (auto& c : s)
            if (c == 'e')
                c = 'E';
        if (s.find_first_of(".E") == std::string::npos)
            s += ".0";
        return s;
    }
    case K::String:
    case K::Uuid:
    case K::Date:
    case K::Uri:
        return quote(osd_string_utf16(v));
    case K::Binary: {
        auto bytes = osd_binary(v);
        std::string s = "[";
        for (auto b : *bytes) {
            if (s.size() > 1)
                s += ',';
            s += std::to_string(b);
        }
        return s + ']';
    }
    case K::Array: {
        std::string s = "[";
        for (const auto& child : v.array) {
            if (s.size() > 1)
                s += ',';
            s += serialize(child, preserve, depth + 1);
        }
        return s + ']';
    }
    case K::Map: {
        std::string s = "{";
        for (const auto& key : v.ordered_keys()) {
            const auto& child = v.get(key);
            if (!preserve && omitted(child))
                continue;
            if (s.size() > 1)
                s += ',';
            s += quote(sdk_map_key_utf16(key)) + ':' + serialize(child, preserve, depth + 1);
        }
        return s + '}';
    }
    }
    return "null";
}

}
std::string osd_json(const Value& value, bool preserve_defaults) {
    return serialize(value, preserve_defaults, 0);
}
Value parse_osd_json_utf16(std::u16string_view text) {
    return read_sdk_json(text);
}
Value parse_osd_json(std::string_view text) {
    return read_sdk_json(framework::utf16_replacing(text));
}
}

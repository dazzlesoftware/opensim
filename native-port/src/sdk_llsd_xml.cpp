// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <algorithm>
#include <limits>
#include <bit>
#include <chrono>
#include <iomanip>
#include <sstream>
namespace opensim::network {
namespace {
using namespace framework;
std::u16string trim(std::u16string value) {
    auto first = std::find_if_not(value.begin(), value.end(), unicode_whitespace);
    auto last = std::find_if_not(value.rbegin(), value.rend(), unicode_whitespace).base();
    return first < last ? std::u16string(first, last) : std::u16string{};
}
AssetDateTime date(std::u16string text) {
    AssetDateTime value;
    if (try_parse_invariant_datetime(std::u16string_view(trim(std::move(text))), value,
                                     shared_native_invariant_datetime_context()->callbacks()))
        return value;
    return AssetDateTime(621355968000000000LL, 0);
}
class Parser {
    std::shared_ptr<NativeNiniXmlReader> reader_;
    const NiniXmlReadToken& token() const {
        return reader_->token();
    }
    bool read() {
        return reader_->read();
    }
    void skip() {
        while (token().node_type == 8 || token().node_type == 13 || token().node_type == 14 ||
               token().node_type == 17)
            read();
    }
    std::u16string read_string() {
        if (reader_->read_state() != NiniXmlReadState::Interactive)
            return {};
        if (token().node_type == 1) {
            if (token().is_empty_element)
                return {};
            if (!read())
                throw std::runtime_error("Invalid ReadString cursor");
        }
        std::u16string value;
        while (token().node_type == 3 || token().node_type == 4 || token().node_type == 13 ||
               token().node_type == 14) {
            value += token().value;
            if (!read())
                break;
        }
        return value;
    }
    Value container(bool map) {
        Value result;
        result.kind = map ? Value::Kind::Map : Value::Kind::Array;
        if (token().is_empty_element) {
            read();
            return result;
        }
        if (read()) {
            while (true) {
                skip();
                if (token().node_type == 15 && token().local_name == (map ? u"map" : u"array"))
                    break;
                if (map) {
                    if (token().node_type != 1 || token().local_name != u"key")
                        throw std::runtime_error("Expected key");
                    auto key = read_string();
                    if (token().node_type != 15 || token().local_name != u"key")
                        throw std::runtime_error("Expected end key");
                    if (!read())
                        throw std::runtime_error("Missing map value");
                    result.set(sdk_map_key_bytes(key), element());
                } else
                    result.array.push_back(element());
            }
            read();
        }
        return result;
    }
    Value element() {
        skip();
        if (token().node_type != 1)
            throw std::runtime_error("Expected element");
        auto name = token().local_name;
        if (name == u"map" || name == u"array")
            return container(name == u"map");
        auto empty = token().is_empty_element;
        Value result;
        if (name == u"undef") {
            read();
            if (empty)
                return result;
            skip();
        } else if (name == u"boolean" || name == u"integer" || name == u"real" || name == u"uuid" ||
                   name == u"date" || name == u"string" || name == u"binary" || name == u"uri") {
            if (name == u"binary" && !empty)
                for (const auto& attribute : token().attributes)
                    if (attribute.name == u"encoding" && attribute.value != u"base64")
                        throw std::runtime_error("Unsupported binary encoding");
            std::u16string value;
            if (!empty) {
                if (read())
                    value = read_string();
            }
            if (name == u"boolean") {
                value = trim(std::move(value));
                result = Value(value == u"true" || value == u"1");
            } else if (name == u"integer") {
                std::int32_t number = 0;
                try {
                    number = invariant_convert_to_int32(trim(std::move(value)));
                } catch (const InvariantNumberError&) {
                }
                result = Value(number);
            } else if (name == u"real") {
                auto text = invariant_lower_utf16(trim(std::move(value)));
                double number = 0;
                if (text == u"nan")
                    number = std::bit_cast<double>(0xfff8000000000000ULL);
                else if (text == u"∞" || text == u"+∞")
                    number = std::numeric_limits<double>::infinity();
                else if (text == u"-∞")
                    number = -std::numeric_limits<double>::infinity();
                else if (text.find(u',') == text.npos && text != u"infinity" && text != u"+infinity" &&
                         text != u"-infinity")
                    try_parse_invariant_real(utf8(text), number);
                result.kind = Value::Kind::Real;
                result.real = number;
            } else if (name == u"uuid") {
                Uuid id;
                try_parse_sdk_uuid(utf8(value), id);
                result.kind = Value::Kind::Uuid;
                result.text = id.str();
            } else if (name == u"date") {
                result.kind = Value::Kind::Date;
                result.sdk_date = std::make_shared<const AssetDateTime>(
                    empty ? AssetDateTime(621355968000000000LL, 0) : date(std::move(value)));
            } else if (name == u"string") {
                result = Value(utf8(value));
                result.sdk_string = std::make_shared<const std::u16string>(std::move(value));
            } else if (name == u"binary") {
                result.kind = Value::Kind::Binary;
                result.text = utf8(trim(std::move(value)));
                osd_binary(result);
            } else {
                static auto backend = native_config_uri_object_backend();
                auto uri = backend->create(std::make_shared<const std::u16string>(std::move(value)),
                                           ConfigUriObjectKind::RelativeOrAbsolute);
                result.kind = Value::Kind::Uri;
                result.sdk_uri = std::move(uri);
            }
            if (empty) {
                read();
                return result;
            }
        } else {
            read();
            result = Value::null_reference();
        }
        if (token().node_type != 15 || token().local_name != name)
            throw std::runtime_error("Unexpected end element");
        read();
        return result;
    }

  public:
    explicit Parser(std::string_view bytes) {
        auto settings = NiniXmlReaderSettings::legacy_document();
        settings.dtd_processing = NiniXmlDtdProcessing::Ignore;
        settings.validating_reader_compatibility = false;
        reader_ = NativeNiniXmlReader::from_bytes(
            {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}, settings);
    }
    explicit Parser(std::u16string_view text) {
        auto settings = NiniXmlReaderSettings::legacy_document();
        settings.dtd_processing = NiniXmlDtdProcessing::Ignore;
        settings.validating_reader_compatibility = false;
        reader_ = NativeNiniXmlReader::from_text(nini_writer_text(std::u16string(text)), settings);
    }
    Value parse() {
        read();
        skip();
        read();
        return element();
    }
};
}
// The SDK dictionary stores UTF16 keys, including lone character-reference
// surrogates accepted by legacy XmlTextReader. Use lossless WTF8 internally.
std::string sdk_map_key_bytes(std::u16string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        std::uint32_t c = value[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < value.size() && value[i + 1] >= 0xdc00 &&
            value[i + 1] <= 0xdfff)
            c = 0x10000 + ((c - 0xd800) << 10) + (value[++i] - 0xdc00);
        if (c < 0x80)
            result += static_cast<char>(c);
        else if (c < 0x800) {
            result += static_cast<char>(0xc0 | (c >> 6));
            result += static_cast<char>(0x80 | (c & 63));
        } else if (c < 0x10000) {
            result += static_cast<char>(0xe0 | (c >> 12));
            result += static_cast<char>(0x80 | ((c >> 6) & 63));
            result += static_cast<char>(0x80 | (c & 63));
        } else {
            result += static_cast<char>(0xf0 | (c >> 18));
            result += static_cast<char>(0x80 | ((c >> 12) & 63));
            result += static_cast<char>(0x80 | ((c >> 6) & 63));
            result += static_cast<char>(0x80 | (c & 63));
        }
    }
    return result;
}
std::u16string sdk_map_key_utf16(std::string_view value) {
    std::u16string result;
    for (std::size_t i = 0; i < value.size();) {
        auto first = static_cast<unsigned char>(value[i++]);
        std::uint32_t c = first;
        int extra = 0;
        if (first >= 0xf0) {
            c = first & 7;
            extra = 3;
        } else if (first >= 0xe0) {
            c = first & 15;
            extra = 2;
        } else if (first >= 0xc0) {
            c = first & 31;
            extra = 1;
        }
        if (i + extra > value.size())
            return framework::utf16_replacing(value);
        while (extra--)
            c = (c << 6) | (static_cast<unsigned char>(value[i++]) & 63);
        if (c <= 0xffff)
            result += static_cast<char16_t>(c);
        else {
            c -= 0x10000;
            result += static_cast<char16_t>(0xd800 + (c >> 10));
            result += static_cast<char16_t>(0xdc00 + (c & 1023));
        }
    }
    return result;
}
std::u16string osd_string_utf16(const Value& input) {
    const auto& value = input.view();
    if (value.kind == Value::Kind::String && value.sdk_string)
        return *value.sdk_string;
    if (value.kind == Value::Kind::Uri && value.sdk_uri)
        return *(value.sdk_uri->is_absolute() ? value.sdk_uri->absolute_uri() : value.sdk_uri->to_string());
    return framework::utf16_replacing(osd_string(value));
}
Value deserialize_sdk_llsd_xml_text(std::u16string_view text) {
    try {
        return Parser(text).parse();
    } catch (...) {
        return {};
    }
}
Value deserialize_sdk_llsd_xml(std::string_view bytes) {
    try {
        return Parser(bytes).parse();
    } catch (...) {
        return {};
    }
}
}

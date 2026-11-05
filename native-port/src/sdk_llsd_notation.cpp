// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/sdk_llsd_notation.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/dotnet_regex_char_class.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::network {
namespace {
using namespace framework;
class NotationParser {
    std::u16string text_;
    std::size_t position_ = 0;
    [[noreturn]] static void fail(std::string message) {
        throw SdkNotationError(std::move(message));
    }
    int peek() const {
        return position_ < text_.size() ? text_[position_] : -1;
    }
    int read() {
        int c = peek();
        if (c >= 0)
            ++position_;
        return c;
    }
    static bool digit(int c) {
        return c >= 0 && dotnet_regex_unicode_category(static_cast<char16_t>(c)) == 8;
    }
    int skip() {
        int c;
        while ((c = peek()) > 0 && (c == 32 || c == 9 || c == 10 || c == 13))
            read();
        return c;
    }
    int next() {
        int c = skip();
        read();
        return c;
    }
    std::u16string block(std::size_t size, const char* error) {
        if (size > text_.size() - position_)
            fail(error);
        auto value = text_.substr(position_, size);
        position_ += size;
        return value;
    }
    static int nibble(int c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        fail("invalid hex char");
    }
    std::u16string quoted(char16_t delimiter) {
        std::u16string value;
        bool escape = false, hex = false, second = false;
        int number = 0;
        while (true) {
            int input = read();
            // Source casts EOF to char and loops appending U+FFFF until allocation failure.
            // Keep this known termination difference explicit instead of exhausting the host.
            if (input < 0)
                fail(
                    "Native notation reader reached EOF in quoted text (source allocation loop remains unimplemented).");
            auto c = static_cast<char16_t>(input);
            if (c == 0)
                break;
            if (escape) {
                if (hex) {
                    if (second) {
                        number = (number << 4) | nibble(c);
                        value += static_cast<char16_t>(number);
                        number = 0;
                        hex = second = escape = false;
                    } else {
                        number = nibble(c);
                        second = true;
                    }
                    continue;
                }
                if (c == u'x') {
                    hex = true;
                    continue;
                }
                switch (c) {
                case u'a':
                    value += u'\a';
                    break;
                case u'b':
                    value += u'\b';
                    break;
                case u'f':
                    value += u'\f';
                    break;
                case u'n':
                    value += u'\n';
                    break;
                case u'r':
                    value += u'\r';
                    break;
                case u't':
                    value += u'\t';
                    break;
                case u'v':
                    value += u'\v';
                    break;
                default:
                    value += c;
                    break;
                }
                escape = false;
            } else {
                if (c == delimiter)
                    break;
                if (c == u'\\')
                    escape = true;
                else
                    value += c;
            }
        }
        return value;
    }
    int length() {
        std::u16string buffer;
        if (skip() == u'(')
            read();
        int c;
        while ((c = read()) > 0 && digit(c) && c != u')')
            buffer += static_cast<char16_t>(c);
        if (c < 0)
            fail("Notation LLSD parsing: Can't parse length value cause unexpected end of stream.");
        try {
            return invariant_convert_to_int32(buffer);
        } catch (...) {
            fail("Notation LLSD parsing: Can't parse length value.");
        }
    }
    static Value string(std::u16string data) {
        Value value(utf8(data));
        value.sdk_string = std::make_shared<const std::u16string>(std::move(data));
        return value;
    }
    Value container(bool map) {
        Value result;
        result.kind = map ? Value::Kind::Map : Value::Kind::Array;
        int c;
        while ((c = skip()) > 0) {
            if (c == (map ? u'}' : u']')) {
                read();
                break;
            }
            if (map) {
                auto key = element();
                if (key.kind != Value::Kind::String)
                    fail("Notation LLSD parsing: Invalid key in map");
                c = next();
                if (c != u':')
                    fail("Notation LLSD parsing: Unexpected end of stream in map.");
                result.set(sdk_map_key_bytes(osd_string_utf16(key)), element());
            } else
                result.array.push_back(element());
            c = next();
            if (c < 0)
                fail(map ? "Notation LLSD parsing: Unexpected end of map discovered."
                         : "Notation LLSD parsing: Unexpected end of array discovered.");
            if (c != (int)u',' && c == (map ? u'}' : u']'))
                break;
        }
        if (c < 0)
            fail(map ? "Notation LLSD parsing: Unexpected end of map discovered."
                     : "Notation LLSD parsing: Unexpected end of array discovered.");
        return result;
    }
    Value element() {
        int marker = next();
        Value result;
        if (marker < 0)
            return result;
        switch (marker) {
        case u'!':
            return result;
        case u'1':
            return Value(true);
        case u'0':
            return Value(false);
        case u't':
        case u'T':
        case u'f':
        case u'F': {
            std::u16string_view full = marker == u't'   ? u"true"
                                       : marker == u'T' ? u"TRUE"
                                       : marker == u'f' ? u"false"
                                                        : u"FALSE";
            std::size_t i = 1;
            while (peek() > 0 && i < full.size() && peek() == full[i]) {
                ++i;
                read();
            }
            if (i > 1 && i < full.size())
                fail("Notation LLSD parsing: True value parsing error:");
            return Value(marker == u't' || marker == u'T');
        }
        case u'i': {
            bool negative = peek() == u'-';
            if (negative)
                read();
            std::uint32_t number = 0;
            while (peek() > 0 && digit(peek()))
                number = number * 10 + static_cast<std::uint32_t>(read() - 48);
            if (negative)
                number = 0 - number;
            return Value(std::bit_cast<std::int32_t>(number));
        }
        case u'r': {
            std::u16string buffer;
            if (peek() == u'-' || peek() == u'+')
                buffer += static_cast<char16_t>(read());
            int c;
            while ((c = peek()) > 0 &&
                   (digit(c) || c == u'.' || c == u'e' || c == u'E' || c == u'+' || c == u'-'))
                buffer += static_cast<char16_t>(read());
            double number;
            if (!try_parse_invariant_real(utf8(buffer), number))
                fail("Notation LLSD parsing: Can't parse real value: " + utf8(buffer));
            result.kind = Value::Kind::Real;
            result.real = number;
            return result;
        }
        case u'u': {
            auto data = block(36, "Notation LLSD parsing: Unexpected end of stream in UUID.");
            Uuid id;
            if (!try_parse_sdk_uuid(utf8(data), id))
                fail("Notation LLSD parsing: Invalid UUID discovered.");
            result.kind = Value::Kind::Uuid;
            result.text = id.str();
            return result;
        }
        case u'b': {
            int c = peek();
            if (c < 0)
                fail("Notation LLSD parsing: Unexpected end of stream in binary.");
            if (c == u'(')
                fail("Notation LLSD parsing: Raw binary encoding not supported.");
            result.kind = Value::Kind::Binary;
            if (digit(c)) {
                auto base = block(2, "Notation LLSD parsing: Unexpected end of stream in binary.");
                int number;
                try {
                    number = invariant_convert_to_int32(base);
                } catch (...) {
                    fail("Notation LLSD parsing: Invalid binary encoding base.");
                }
                if (number != 64)
                    fail("Notation LLSD parsing: Encoding base" + std::to_string(number) +
                         " + not supported.");
                if (read() < 0)
                    fail("Notation LLSD parsing: Unexpected end of stream in binary.");
                result.text = utf8(quoted(u'"'));
                try {
                    osd_binary(result);
                } catch (const std::invalid_argument&) {
                    fail(
                        "The input is not a valid Base-64 string as it contains a non-base 64 character, more than two padding characters, or an illegal character among the padding characters.");
                }
            } else
                result.binary_data = std::make_shared<std::vector<std::uint8_t>>();
            return result;
        }
        case u's': {
            int size = length();
            if (read() < 0)
                fail("Notation LLSD parsing: Unexpected end of stream in string.");
            auto data = block(static_cast<std::size_t>(size),
                              "Notation LLSD parsing: Unexpected end of stream in string.");
            if (read() < 0)
                fail("Notation LLSD parsing: Unexpected end of stream in string.");
            return string(std::move(data));
        }
        case u'\'':
        case u'"':
            return string(quoted(static_cast<char16_t>(marker)));
        case u'l': {
            if (read() < 0)
                fail("Notation LLSD parsing: Unexpected end of stream in string.");
            auto data = quoted(u'"');
            try {
                static auto backend = native_config_uri_object_backend();
                result.sdk_uri = backend->create(std::make_shared<const std::u16string>(std::move(data)),
                                                 ConfigUriObjectKind::RelativeOrAbsolute);
            } catch (...) {
                fail("Notation LLSD parsing: Invalid Uri format detected.");
            }
            result.kind = Value::Kind::Uri;
            return result;
        }
        case u'd': {
            if (read() < 0)
                fail("Notation LLSD parsing: Unexpected end of stream in date.");
            auto data = quoted(u'"');
            AssetDateTime date;
            if (!try_parse_invariant_datetime(std::u16string_view(data), date,
                                              shared_native_invariant_datetime_context()->callbacks()))
                fail("Notation LLSD parsing: Invalid date discovered.");
            result.kind = Value::Kind::Date;
            result.sdk_date = std::make_shared<const AssetDateTime>(date);
            return result;
        }
        case u'[':
            return container(false);
        case u'{':
            return container(true);
        default:
            fail("Notation LLSD parsing: Unknown type marker '" +
                 utf8(std::u16string(1, static_cast<char16_t>(marker))) + "'.");
        }
    }

  public:
    explicit NotationParser(std::string_view bytes)
        : text_(decode_stream_reader_text({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()})
                    .text) {
    }
    explicit NotationParser(std::u16string_view text) : text_(text) {
    }
    Value parse() {
        int c = skip();
        if (c < 0)
            return {};
        if (c == u'<')
            position_ += std::min<std::size_t>(19, text_.size() - position_);
        return element();
    }
};
}
Value deserialize_sdk_llsd_notation_text(std::u16string_view text) {
    return NotationParser(text).parse();
}
Value deserialize_sdk_llsd_notation(std::string_view bytes) {
    return NotationParser(bytes).parse();
}
}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/sdk_llsd_binary.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace opensim::network {
namespace {
class BinaryParser {
    std::string_view bytes_;
    std::size_t position_ = 0;
    [[noreturn]] static void fail(const char* text) {
        throw std::runtime_error(text);
    }
    int read() {
        return position_ < bytes_.size() ? static_cast<unsigned char>(bytes_[position_++]) : -1;
    }
    void whitespace() {
        int c;
        while ((c = read()) > 0 && (c == 32 || c == 9 || c == 10 || c == 13)) {
        }
        if (position_ > 0)
            --position_;
    }
    bool find(std::string_view text) {
        auto saved = position_;
        std::size_t matched = 0;
        bool equal = true;
        int c;
        // Source FindString reads one byte beyond the matched header, then seeks back.
        while (equal && (c = read()) > 0 && matched < text.size()) {
            int expected = text[matched];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (expected >= 'A' && expected <= 'Z')
                expected += 32;
            equal = c == expected;
            if (equal)
                ++matched;
        }
        if (equal && matched == text.size()) {
            if (position_)
                --position_;
            return true;
        }
        position_ = saved;
        return false;
    }
    bool find_byte(int expected) {
        int c = read();
        if (c < 0)
            return false;
        if (c == expected)
            return true;
        --position_;
        return false;
    }
    std::string_view consume(std::int32_t count) {
        if (count < 0)
            fail("Arithmetic operation resulted in an overflow.");
        auto size = static_cast<std::size_t>(count);
        if (size > bytes_.size() - position_)
            fail("Binary LLSD parsing: Unexpected end of stream.");
        auto result = bytes_.substr(position_, size);
        position_ += size;
        return result;
    }
    std::uint64_t number(int count, bool little = false) {
        auto data = consume(count);
        std::uint64_t value = 0;
        if (little) {
            for (int i = count - 1; i >= 0; --i)
                value = (value << 8) | static_cast<unsigned char>(data[i]);
        } else
            for (unsigned char c : data)
                value = (value << 8) | c;
        return value;
    }
    std::int32_t length() {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(number(4)));
    }
    std::u16string string() {
        return framework::utf16_replacing(consume(length()));
    }
    Value element() {
        whitespace();
        int marker = read();
        if (marker < 0)
            fail("Binary LLSD parsing: Unexpected end of stream.");
        Value result;
        switch (marker) {
        case '!':
            return result;
        case '0':
            return Value(false);
        case '1':
            return Value(true);
        case 'i':
            return Value(length());
        case 'r':
            result.kind = Value::Kind::Real;
            result.real = std::bit_cast<double>(number(8));
            return result;
        case 'u': {
            auto data = consume(16);
            constexpr char hex[] = "0123456789abcdef";
            result.kind = Value::Kind::Uuid;
            for (std::size_t i = 0; i < 16; ++i) {
                if (i == 4 || i == 6 || i == 8 || i == 10)
                    result.text += '-';
                auto c = static_cast<unsigned char>(data[i]);
                result.text += hex[c >> 4];
                result.text += hex[c & 15];
            }
            return result;
        }
        case 'b': {
            auto data = consume(length());
            result.kind = Value::Kind::Binary;
            result.binary_data = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
            return result;
        }
        case 's': {
            auto data = string();
            result = Value(framework::utf8(data));
            result.sdk_string = std::make_shared<const std::u16string>(std::move(data));
            return result;
        }
        case 'l': {
            auto data = string();
            try {
                static auto backend = framework::native_config_uri_object_backend();
                result.sdk_uri = backend->create(std::make_shared<const std::u16string>(std::move(data)),
                                                 framework::ConfigUriObjectKind::RelativeOrAbsolute);
            } catch (...) {
                fail("Binary LLSD parsing: Invalid Uri format detected.");
            }
            result.kind = Value::Kind::Uri;
            return result;
        }
        case 'd': {
            double seconds = std::bit_cast<double>(number(8, true));
            constexpr std::int64_t epoch = 621355968000000000LL;
            constexpr std::int64_t maximum = 3155378975999999999LL;
            if (std::isnan(seconds))
                fail(
                    "The added or subtracted value results in an un-representable DateTime. (Parameter 'value')");
            if (std::abs(seconds) > 315537897599.0)
                fail("Value to add was out of range. (Parameter 'value')");
            auto whole = static_cast<std::int64_t>(seconds);
            auto fraction = seconds - static_cast<double>(whole);
            auto ticks = whole * 10000000 + static_cast<std::int64_t>(fraction * 10000000);
            if (ticks < -epoch || ticks > maximum - epoch)
                fail(
                    "The added or subtracted value results in an un-representable DateTime. (Parameter 'value')");
            result.kind = Value::Kind::Date;
            result.sdk_date = std::make_shared<const framework::AssetDateTime>(
                framework::shared_native_invariant_datetime_context()->to_local_time(
                    framework::AssetDateTime(epoch + ticks, 1)));
            return result;
        }
        case '[': {
            auto count = length();
            result.kind = Value::Kind::Array;
            for (std::int32_t i = 0; i < count; ++i)
                result.array.push_back(element());
            if (!find_byte(']'))
                fail("Binary LLSD parsing: Missing end marker in array.");
            return result;
        }
        case '{': {
            auto count = length();
            result.kind = Value::Kind::Map;
            for (std::int32_t i = 0; i < count; ++i) {
                if (!find_byte('k'))
                    fail("Binary LLSD parsing: Missing key marker in map.");
                auto key = string();
                result.set(sdk_map_key_bytes(key), element());
            }
            if (!find_byte('}'))
                fail("Binary LLSD parsing: Missing end marker in map.");
            return result;
        }
        default:
            fail("Binary LLSD parsing: Unknown type marker.");
        }
    }

  public:
    explicit BinaryParser(std::string_view bytes) : bytes_(bytes) {
    }
    Value parse() {
        whitespace();
        if (!find("<? llsd/binary ?>"))
            find("<?llsd/binary?>");
        whitespace();
        return element();
    }
};
}
Value deserialize_sdk_llsd_binary(std::string_view bytes) {
    return BinaryParser(bytes).parse();
}
}

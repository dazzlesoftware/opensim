// Adapted from runtime v8.0.31 X500NameEncoder.cs and OidLookup.cs.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_x500_name.hpp"
#include "opensim/framework/text.hpp"
#include <openssl/objects.h>
#include <openssl/err.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
namespace opensim::framework {
namespace {
using Bytes = std::span<const std::uint8_t>;
struct Invalid {};
[[noreturn]] void invalid() { throw Invalid{}; }
struct Value {
    unsigned tag;
    Bytes contents, encoded;
};
struct Der {
    Bytes data;
    Value take() {
        if (data.size() < 2)
            invalid();
        std::size_t at = 1;
        const auto tag = data[0];
        if ((tag & 31) == 31) {
            if (data[at] == 0x80)
                invalid();
            std::uint64_t number = 0;
            bool ended = false;
            while (at < data.size()) {
                auto b = data[at++];
                number = (number << 7) | (b & 127);
                if (number > std::numeric_limits<std::int32_t>::max())
                    invalid();
                if (!(b & 128)) {
                    ended = true;
                    break;
                }
            }
            if (!ended || number < 31)
                invalid();
        }
        if (at == data.size())
            invalid();
        std::size_t length = data[at++];
        if (length & 128) {
            auto count = length & 127;
            if (!count || count > sizeof(std::size_t) || count > data.size() - at || data[at] == 0)
                invalid();
            length = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (length > (std::numeric_limits<std::size_t>::max() >> 8))
                    invalid();
                length = (length << 8) | data[at++];
            }
            if (length < 128)
                invalid();
        }
        if (length > data.size() - at)
            invalid();
        Value value{tag, data.subspan(at, length), data.first(at + length)};
        data = data.subspan(at + length);
        return value;
    }
    Bytes take(unsigned tag) {
        auto value = take();
        if (value.tag != tag)
            invalid();
        return value.contents;
    }
    void end() {
        if (!data.empty())
            invalid();
    }
};
std::string oid(const Value &value) {
    if (value.tag != 6 || value.contents.empty())
        invalid();
    bool start = true;
    unsigned bits = 0;
    for (auto b : value.contents) {
        if (start) {
            if (b == 128)
                invalid();
            bits = 0;
            for (unsigned n = b & 127; n; n >>= 1)
                ++bits;
        } else
            bits += 7;
        if (bits > 128)
            invalid();
        start = !(b & 128);
    }
    if (!start)
        invalid();
    const auto *pointer = value.encoded.data();
    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(
        d2i_ASN1_OBJECT(nullptr, &pointer, static_cast<long>(value.encoded.size())), ASN1_OBJECT_free);
    if (!object) {
        ERR_clear_error();
        invalid();
    }
    int length = OBJ_obj2txt(nullptr, 0, object.get(), 1);
    if (length <= 0)
        invalid();
    std::string text(static_cast<std::size_t>(length) + 1, '\0');
    OBJ_obj2txt(text.data(), length + 1, object.get(), 1);
    text.resize(length);
    return text;
}
std::u16string oid_label(const std::string &value) {
    struct Entry {
        std::string_view oid, name;
    };
    static constexpr Entry entries[] = {
#include "config_http_linux_x500_oids.inc"
    };
    for (const auto &entry : entries)
        if (entry.oid == value)
            return utf16(entry.name);
    ERR_clear_error();
    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(OBJ_txt2obj(value.c_str(), 1),
                                                                     ASN1_OBJECT_free);
    if (object) {
        const int nid = OBJ_obj2nid(object.get());
        if (nid != NID_undef)
            if (const auto *name = OBJ_nid2ln(nid); name && *name && value != name)
                return utf16(name);
    }
    ERR_clear_error();
    return u"OID." + utf16(value);
}
std::u16string binary(Bytes data) {
    constexpr char16_t hex[] = u"0123456789ABCDEF";
    std::u16string result = u"#";
    for (auto b : data) {
        result.push_back(hex[b >> 4]);
        result.push_back(hex[b & 15]);
    }
    return result;
}
std::u16string string_value(const Value &value, bool &fallback) {
    fallback = false;
    std::u16string result;
    switch (value.tag & 0xdf) {
    case 12:
    case 18:
    case 19:
    case 20:
    case 22:
    case 30: {
        // DER character strings cannot use constructed encoding.
        if (value.tag & 32)
            invalid();
        auto bytes = value.contents;
        if (value.tag == 30) {
            if (bytes.size() % 2)
                invalid();
            for (std::size_t i = 0; i < bytes.size(); i += 2) {
                char16_t c = static_cast<char16_t>((bytes[i] << 8) | bytes[i + 1]);
                if (c >= 0xd800 && c <= 0xdfff)
                    invalid();
                result.push_back(c);
            }
        } else if (value.tag == 12 || value.tag == 20) {
            try {
                result = utf16(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
            } catch (const std::invalid_argument &) {
                if (value.tag != 20)
                    invalid();
                for (auto b : bytes)
                    result.push_back(b);
            }
        } else {
            constexpr std::string_view printable = " '()+,-./:=?";
            for (auto b : bytes) {
                if (value.tag == 18 && b != ' ' && (b < '0' || b > '9'))
                    invalid();
                if (value.tag == 19 && !(b >= 'A' && b <= 'Z') && !(b >= 'a' && b <= 'z') &&
                    !(b >= '0' && b <= '9') && printable.find(static_cast<char>(b)) == printable.npos)
                    invalid();
                if (value.tag == 22 && b > 127)
                    invalid();
                result.push_back(b);
            }
        }
        while (!result.empty() && result.back() == 0)
            result.pop_back();
        return result;
    }
    case 4:
        if (value.tag & 32)
            invalid();
        fallback = true;
        return binary(value.contents);
    default:
        fallback = true;
        return binary(value.encoded);
    }
}
bool whitespace(char16_t c) { return c == u' ' || (c >= u'\t' && c <= u'\r'); }
bool quote(std::u16string_view value) {
    return value.empty() || whitespace(value.front()) || whitespace(value.back()) ||
           value.find_first_of(u",+=\"\n<>#;") != value.npos;
}
}
std::u16string config_http_linux_x500_name(Bytes bytes) {
    try {
        Der input{bytes};
        Der name{input.take(0x30)};
        input.end();
        std::vector<Bytes> rdns;
        while (!name.data.empty())
            rdns.push_back(name.take(0x31));
        std::u16string result;
        bool first_rdn = true;
        for (auto rdn = rdns.rbegin(); rdn != rdns.rend(); ++rdn) {
            if (!first_rdn)
                result += u", ";
            first_rdn = false;
            Der set{*rdn};
            bool first_attribute = true;
            while (!set.data.empty()) {
                Der attribute{set.take(0x30)};
                auto label = oid(attribute.take());
                bool fallback = false;
                auto value = string_value(attribute.take(), fallback);
                attribute.end();
                if (!first_attribute)
                    result += u" + ";
                first_attribute = false;
                result += oid_label(label);
                result += u'=';
                const bool quoted = !fallback && quote(value);
                if (quoted)
                    result += u'"';
                for (auto c : value) {
                    result += c;
                    if (quoted && c == u'"')
                        result += c;
                }
                if (quoted)
                    result += u'"';
            }
        }
        return result;
    } catch (const Invalid &) {
        return {};
    }
}
}

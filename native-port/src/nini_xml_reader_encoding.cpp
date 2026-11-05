// Port of pinned XmlTextReaderImpl.DetectEncoding, SafeAsciiDecoder,
// UTF16Decoder/Ucs4Decoder and consumed default EncodingTable/decoder semantics.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/nini_xml_reader_encoding.hpp"
#include <algorithm>
namespace opensim::framework {
namespace {
using Kind = NiniXmlEncodingKind;
std::uint32_t scalar(std::span<const std::uint8_t> b, Kind kind) {
    if (kind == Kind::Ucs4BE || kind == Kind::Utf32BE)
        return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | b[3];
    if (kind == Kind::Ucs42143)
        return (std::uint32_t(b[1]) << 24) | (std::uint32_t(b[0]) << 16) | (std::uint32_t(b[3]) << 8) | b[2];
    if (kind == Kind::Ucs43412)
        return (std::uint32_t(b[2]) << 24) | (std::uint32_t(b[3]) << 16) | (std::uint32_t(b[0]) << 8) | b[1];
    return (std::uint32_t(b[3]) << 24) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[1]) << 8) | b[0];
}
void append_scalar(std::u16string& text, std::uint32_t value, bool custom) {
    if (value <= 65535)
        text += static_cast<char16_t>(
            value); // Preserve Ucs4Decoder.Ucs4ToUTF16 literally: the plane term is added without a 6-bit shift.
    // Actual pinned BCL produces D801 DC00 for BOM-detected U+20000, but D840 DC00 after a declared utf-32 switch.
    else if (custom) {
        text += static_cast<char16_t>(55296 + static_cast<std::uint16_t>((value >> 16) - 1) +
                                      static_cast<std::uint16_t>((value >> 10) & 63));
        text += static_cast<char16_t>(56320 + static_cast<std::uint16_t>(value & 1023));
    } else {
        value -= 65536;
        text += static_cast<char16_t>(0xd800 + (value >> 10));
        text += static_cast<char16_t>(0xdc00 + (value & 1023));
    }
}
}
NiniXmlEncoding nini_xml_detect_encoding(std::span<const std::uint8_t> bytes) {
    NiniXmlEncoding e;
    if (bytes.size() < 2)
        return e;
    auto first = (unsigned(bytes[0]) << 8) | bytes[1];
    auto second = bytes.size() >= 4 ? (unsigned(bytes[2]) << 8) | bytes[3] : 0;
    switch (first) {
    case 0:
        if (second == 0xfeff || second == 0x003c)
            e = {Kind::Ucs4BE, second == 0xfeff ? 4u : 0u, "ucs-4 (Bigendian)"};
        else if (second == 0xfffe || second == 0x3c00)
            e = {Kind::Ucs42143, second == 0xfffe ? 4u : 0u, "ucs-4 (2143)"};
        break;
    case 0xfeff:
        e = second == 0 ? NiniXmlEncoding{Kind::Ucs43412, 4, "ucs-4 (3412)"}
                        : NiniXmlEncoding{Kind::Utf16BE, 2, "utf-16BE"};
        break;
    case 0xfffe:
        e = second == 0 ? NiniXmlEncoding{Kind::Ucs4LE, 4, "ucs-4 (Littleendian)"}
                        : NiniXmlEncoding{Kind::Utf16LE, 2, "utf-16"};
        break;
    case 0x3c00:
        e = second == 0 ? NiniXmlEncoding{Kind::Ucs4LE, 0, "ucs-4 (Littleendian)"}
                        : NiniXmlEncoding{Kind::Utf16LE, 0, "utf-16"};
        break;
    case 0x003c:
        e = second == 0 ? NiniXmlEncoding{Kind::Ucs43412, 0, "ucs-4 (3412)"}
                        : NiniXmlEncoding{Kind::Utf16BE, 0, "utf-16BE"};
        break;
    case 0x4c6f:
        if (second == 0xa794)
            e = {Kind::Ebcdic, 0, "ebcdic"};
        break;
    case 0xefbb:
        if ((second & 0xff00) == 0xbf00)
            e = {Kind::Utf8Strict, 3, "utf-8"};
        break;
    }
    if (e.kind == Kind::SafeAscii && bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
        bytes[2] == 0xbf)
        e.preamble = 3;
    if (e.preamble > bytes.size())
        e.preamble = 0;
    return e;
}
std::optional<NiniXmlEncoding> nini_xml_builtin_encoding(std::u16string_view name) {
    std::u16string lower(name);
    for (auto& c : lower)
        if (c >= u'A' && c <= u'Z')
            c += 32;
    struct Entry {
        std::u16string_view name;
        int page;
    };
    static constexpr Entry entries[] = {{u"ansi_x3.4-1968", 20127},
                                        {u"ansi_x3.4-1986", 20127},
                                        {u"ascii", 20127},
                                        {u"cp367", 20127},
                                        {u"cp819", 28591},
                                        {u"csascii", 20127},
                                        {u"csisolatin1", 28591},
                                        {u"csunicode11utf7", 65000},
                                        {u"ibm367", 20127},
                                        {u"ibm819", 28591},
                                        {u"iso-10646-ucs-2", 1200},
                                        {u"iso-8859-1", 28591},
                                        {u"iso-ir-100", 28591},
                                        {u"iso-ir-6", 20127},
                                        {u"iso646-us", 20127},
                                        {u"iso8859-1", 28591},
                                        {u"iso_646.irv:1991", 20127},
                                        {u"iso_8859-1", 28591},
                                        {u"iso_8859-1:1987", 28591},
                                        {u"l1", 28591},
                                        {u"latin1", 28591},
                                        {u"ucs-2", 1200},
                                        {u"unicode", 1200},
                                        {u"unicode-1-1-utf-7", 65000},
                                        {u"unicode-1-1-utf-8", 65001},
                                        {u"unicode-2-0-utf-7", 65000},
                                        {u"unicode-2-0-utf-8", 65001},
                                        {u"unicodefffe", 1201},
                                        {u"us", 20127},
                                        {u"us-ascii", 20127},
                                        {u"utf-16", 1200},
                                        {u"utf-16be", 1201},
                                        {u"utf-16le", 1200},
                                        {u"utf-32", 12000},
                                        {u"utf-32be", 12001},
                                        {u"utf-32le", 12000},
                                        {u"utf-7", 65000},
                                        {u"utf-8", 65001},
                                        {u"x-unicode-1-1-utf-7", 65000},
                                        {u"x-unicode-1-1-utf-8", 65001},
                                        {u"x-unicode-2-0-utf-7", 65000},
                                        {u"x-unicode-2-0-utf-8", 65001}};
    for (auto e : entries)
        if (e.name == lower)
            switch (e.page) {
            case 1200:
                return NiniXmlEncoding{Kind::Utf16LE, 0, "utf-16"};
            case 1201:
                return NiniXmlEncoding{Kind::Utf16BE, 0, "utf-16BE"};
            case 12000:
                return NiniXmlEncoding{Kind::Utf32LE, 0, "utf-32"};
            case 12001:
                return NiniXmlEncoding{Kind::Utf32BE, 0, "utf-32BE"};
            case 20127:
                return NiniXmlEncoding{Kind::Ascii, 0, "us-ascii"};
            case 28591:
                return NiniXmlEncoding{Kind::Latin1, 0, "iso-8859-1"};
            case 65001:
                return NiniXmlEncoding{lower == u"utf-8" ? Kind::Utf8Strict : Kind::Utf8Replacement, 0,
                                       "utf-8"};
            default:
                return {};
            }
    return {};
}
std::size_t nini_xml_encoded_byte_count(std::u16string_view text, Kind kind) {
    if (kind == Kind::Utf16LE || kind == Kind::Utf16BE)
        return text.size() * 2;
    if (kind == Kind::Ucs4LE || kind == Kind::Ucs4BE || kind == Kind::Ucs42143 || kind == Kind::Ucs43412)
        return text.size() * 4;
    if (kind == Kind::Ascii || kind == Kind::Latin1)
        return text.size();
    if (kind == Kind::Utf32LE || kind == Kind::Utf32BE) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] >= 0xd800 && text[i] <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 &&
                text[i + 1] <= 0xdfff)
                ++i;
            count += 4;
        }
        return count;
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = text[i];
        if (c < 128)
            ++count;
        else if (c < 2048)
            count += 2;
        else if (c >= 0xd800 && c <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 &&
                 text[i + 1] <= 0xdfff) {
            count += 4;
            ++i;
        } else
            count += 3;
    }
    return count;
}
NiniXmlDecodeResult NiniXmlDecoder::convert(std::span<const std::uint8_t> input, std::size_t maximum) {
    NiniXmlDecodeResult out;
    if (maximum == 0)
        return out;
    const bool utf8 = kind_ == Kind::Utf8Strict || kind_ == Kind::Utf8Replacement;
    const bool utf16 = kind_ == Kind::Utf16LE || kind_ == Kind::Utf16BE;
    const bool custom =
        kind_ == Kind::Ucs4LE || kind_ == Kind::Ucs4BE || kind_ == Kind::Ucs42143 || kind_ == Kind::Ucs43412;
    std::size_t units = 0;
    while (out.bytes_used < input.size() || pending_count_) {
        if (!custom && out.text.size() >= maximum)
            break;
        if (custom && units >= maximum)
            break;
        const auto begin = out.bytes_used;
        std::array<std::uint8_t, 4> sequence = pending_;
        std::size_t have = pending_count_;
        bool had_pending = have != 0;
        std::size_t needed = utf8    ? (have ? sequence[0] >= 0xf0   ? 4
                                               : sequence[0] >= 0xe0 ? 3
                                                                     : 2
                                             : 1)
                             : utf16 ? 2
                             : custom || kind_ == Kind::Utf32LE || kind_ == Kind::Utf32BE ? 4
                                                                                          : 1;
        if (!have) {
            if (out.bytes_used == input.size())
                break;
            sequence[have++] = input[out.bytes_used++];
            if (utf8) {
                auto c = sequence[0];
                needed = c < 128                  ? 1
                         : c >= 0xc2 && c <= 0xdf ? 2
                         : c >= 0xe0 && c <= 0xef ? 3
                         : c >= 0xf0 && c <= 0xf4 ? 4
                                                  : 1;
            }
        }
        if (needed == 1) {
            auto c = sequence[0];
            if (utf8 && c >= 128) {
                if (kind_ == Kind::Utf8Strict) {
                    out.bytes_used = begin;
                    if (out.text.empty())
                        out.fault = NiniXmlDecodeFault::Argument;
                    return out;
                }
                out.text += u'\ufffd';
            } else
                out.text += kind_ == Kind::Ascii && c >= 128 ? u'?' : static_cast<char16_t>(c);
            pending_count_ = 0;
            ++units;
            continue;
        }
        bool invalid = false;
        while (have < needed && out.bytes_used < input.size()) {
            auto c = input[out.bytes_used];
            if (utf8) {
                bool valid = c >= 0x80 && c <= 0xbf;
                if (have == 1) {
                    if (sequence[0] == 0xe0 && c < 0xa0)
                        valid = false;
                    if (sequence[0] == 0xed && c > 0x9f)
                        valid = false;
                    if (sequence[0] == 0xf0 && c < 0x90)
                        valid = false;
                    if (sequence[0] == 0xf4 && c > 0x8f)
                        valid = false;
                }
                if (!valid) {
                    invalid = true;
                    break;
                }
            }
            sequence[have++] = c;
            ++out.bytes_used;
        }
        if (invalid) {
            pending_count_ = 0;
            if (kind_ == Kind::Utf8Strict) {
                out.bytes_used = begin;
                if (out.text.empty())
                    out.fault = NiniXmlDecodeFault::Argument;
                return out;
            }
            out.text += u'\ufffd';
            ++units;
            continue;
        }
        if (have < needed) {
            pending_ = sequence;
            pending_count_ = have;
            break;
        }
        std::uint32_t value = 0;
        if (utf8) {
            value = sequence[0] & ((1u << (7 - needed)) - 1);
            for (std::size_t i = 1; i < needed; ++i)
                value = (value << 6) | (sequence[i] & 63);
        } else if (utf16)
            value = kind_ == Kind::Utf16LE ? unsigned(sequence[0]) | (unsigned(sequence[1]) << 8)
                                           : (unsigned(sequence[0]) << 8) | sequence[1];
        else
            value = scalar(sequence, kind_);
        if (!custom && value > 65535 && value <= 0x10ffff && out.text.size() + 2 > maximum) {
            out.bytes_used = begin;
            break;
        }
        if (custom && (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))) {
            pending_count_ = 0;
            if (value >= 0xd800 && value <= 0xdfff) {
                out.fault = NiniXmlDecodeFault::Xml;
                return out;
            }
            out.bytes_used = begin;
            if (out.text.empty())
                out.fault = NiniXmlDecodeFault::Argument;
            return out;
        }
        if (!custom && !utf16 && (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)))
            value = 0xfffd;
        append_scalar(out.text, value, custom);
        pending_count_ = 0;
        units += custom && had_pending && value > 65535 ? 2 : 1;
    }
    return out;
}
}

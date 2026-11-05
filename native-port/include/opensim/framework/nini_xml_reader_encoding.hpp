// Source-derived XML decoder layer, .NET runtime v8.0.31.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
namespace opensim::framework {
enum class NiniXmlEncodingKind {
    SafeAscii,
    Utf8Strict,
    Utf8Replacement,
    Utf16LE,
    Utf16BE,
    Ucs4LE,
    Ucs4BE,
    Ucs42143,
    Ucs43412,
    Utf32LE,
    Utf32BE,
    Ascii,
    Latin1,
    Ebcdic
};
struct NiniXmlEncoding {
    NiniXmlEncodingKind kind = NiniXmlEncodingKind::SafeAscii;
    std::size_t preamble = 0;
    std::string_view web_name = "utf-8";
};
NiniXmlEncoding nini_xml_detect_encoding(std::span<const std::uint8_t>);
std::optional<NiniXmlEncoding> nini_xml_builtin_encoding(std::u16string_view);
std::size_t nini_xml_encoded_byte_count(std::u16string_view, NiniXmlEncodingKind);
enum class NiniXmlDecodeFault { None, Argument, Xml };
struct NiniXmlDecodeResult {
    std::u16string text;
    std::size_t bytes_used = 0;
    NiniXmlDecodeFault fault = NiniXmlDecodeFault::None;
};
// Convert deliberately never flushes on end-of-input, as XmlTextReaderImpl does.
// Its caller owns the source's 80-character append and later buffer budgets.
class NiniXmlDecoder {
    NiniXmlEncodingKind kind_;
    std::array<std::uint8_t, 4> pending_{};
    std::size_t pending_count_ = 0;

public:
    explicit NiniXmlDecoder(NiniXmlEncodingKind kind) : kind_(kind) {}
    NiniXmlDecodeResult convert(std::span<const std::uint8_t>, std::size_t max_characters);
};
}

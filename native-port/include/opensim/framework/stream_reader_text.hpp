// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
namespace opensim::framework {
enum class StreamReaderEncoding : std::int32_t {
    Utf8 = 65001,
    Utf16LE = 1200,
    Utf16BE = 1201,
    Utf32LE = 12000,
    Utf32BE = 12001
};
struct StreamReaderText {
    std::u16string text;
    StreamReaderEncoding encoding = StreamReaderEncoding::Utf8;
    std::size_t bom_bytes = 0;
};
enum class StreamReaderTextFailure { Open, Read };
class StreamReaderTextError : public std::runtime_error {
public:
    StreamReaderTextFailure kind;
    std::error_code system_error;
    StreamReaderTextError(StreamReaderTextFailure, const std::filesystem::path&, std::error_code);
};
// Default StreamReader UTF8 fallback with automatic UTF8/16/32 BOM detection.
// Text is preserved verbatim: no newline normalization or second BOM removal.
StreamReaderText decode_stream_reader_text(std::span<const std::uint8_t>);
StreamReaderText read_stream_reader_text(const std::filesystem::path&);
// Separate BCL ReadLine behavior; Nini tokenization consumes text instead.
std::vector<std::u16string> stream_reader_lines(std::u16string_view);
}

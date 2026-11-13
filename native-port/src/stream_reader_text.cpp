// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/full_path.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace opensim::framework {
namespace {
std::string path_text(const std::filesystem::path& path) {
    auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
void append(std::u16string& out, std::uint32_t value) {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        out += u'\ufffd';
        return;
    }
    if (value < 0x10000)
        out += static_cast<char16_t>(value);
    else {
        value -= 0x10000;
        out += static_cast<char16_t>(0xd800 + (value >> 10));
        out += static_cast<char16_t>(0xdc00 + (value & 1023));
    }
}
#ifdef _WIN32
// Windows stream path adaptation follows dotnet/runtime v8.0.31
// PathInternal.Windows.cs and SafeFileHandle.Windows.cs.
// Copyright (c) .NET Foundation and Contributors. Licensed under MIT;
// see native-port/third_party/dotnet_collation/LICENSE.
bool separator(wchar_t c) {
    return c == L'\\' || c == L'/';
}
bool extended(std::wstring_view path) {
    return path.size() >= 4 && path[0] == L'\\' && (path[1] == L'\\' || path[1] == L'?') && path[2] == L'?' &&
           path[3] == L'\\';
}
bool device(std::wstring_view path) {
    return extended(path) || (path.size() >= 4 && separator(path[0]) && separator(path[1]) &&
                              (path[2] == L'.' || path[2] == L'?') && separator(path[3]));
}
bool valid_drive(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}
bool partially_qualified(std::wstring_view path) {
    if (path.size() < 2)
        return true;
    if (separator(path[0]))
        return !(path[1] == L'?' || separator(path[1]));
    return !(path.size() >= 3 && path[1] == L':' && separator(path[2]) && valid_drive(path[0]));
}
std::size_t root_length(std::wstring_view path) {
    auto length = path.size();
    std::size_t i = 0;
    bool is_device = device(path);
    bool unc = is_device && length >= 8 && separator(path[7]) && path[4] == L'U' && path[5] == L'N' &&
               path[6] == L'C';
    if ((!is_device || unc) && length && separator(path[0])) {
        if (unc || (length > 1 && separator(path[1]))) {
            i = unc ? 8 : 2;
            int remaining = 2;
            for (; i < length; ++i)
                if (separator(path[i]) && --remaining <= 0)
                    break;
        } else
            i = 1;
    } else if (is_device) {
        for (i = 4; i < length && !separator(path[i]); ++i) {
        }
        if (i < length && i > 4 && separator(path[i]))
            ++i;
    } else if (length >= 2 && path[1] == L':' && valid_drive(path[0])) {
        i = 2;
        if (length > 2 && separator(path[2]))
            ++i;
    }
    return i;
}
struct WindowsStreamPath {
    std::wstring full, io;
};
WindowsStreamPath windows_stream_path(const std::filesystem::path& path) {
    std::u16string normalized;
    try {
        normalized = utf16(get_full_path(path_text(path)));
    } catch (const FullPathError& error) {
        auto code =
            error.win32_code.value_or(error.kind == FullPathFailure::PathTooLong ? ERROR_FILENAME_EXCED_RANGE
                                                                                 : ERROR_INVALID_PARAMETER);
        throw StreamReaderTextError(StreamReaderTextFailure::Open, path,
                                    std::error_code(static_cast<int>(code), std::system_category()));
    }
    WindowsStreamPath result;
    result.full.assign(normalized.begin(), normalized.end());
    result.io = result.full;
    if ((result.io.size() >= 260 ||
         (!result.io.empty() && (result.io.back() == L'.' || result.io.back() == L' '))) &&
        !partially_qualified(result.io) && !device(result.io)) {
        if (result.io.starts_with(L"\\\\"))
            result.io = L"\\\\?\\UNC\\" + result.io.substr(2);
        else
            result.io = L"\\\\?\\" + result.io;
    }
    return result;
}
#endif

}
StreamReaderTextError::StreamReaderTextError(StreamReaderTextFailure failure,
                                             const std::filesystem::path& path, std::error_code error)
    : runtime_error(std::string(failure == StreamReaderTextFailure::Open ? "Cannot open text file: "
                                                                         : "Cannot read text file: ") +
                    path_text(path) + " (error " + std::to_string(error.value()) + ")"),
      kind(failure), system_error(error) {
}
StreamReaderText decode_stream_reader_text(std::span<const std::uint8_t> bytes) {
    StreamReaderText result;
    auto starts = [&](std::initializer_list<std::uint8_t> prefix) {
        return bytes.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), bytes.begin());
    };
    if (starts({0xff, 0xfe, 0, 0})) {
        result.encoding = StreamReaderEncoding::Utf32LE;
        result.bom_bytes = 4;
    } else if (starts({0, 0, 0xfe, 0xff})) {
        result.encoding = StreamReaderEncoding::Utf32BE;
        result.bom_bytes = 4;
    } else if (starts({0xff, 0xfe})) {
        result.encoding = StreamReaderEncoding::Utf16LE;
        result.bom_bytes = 2;
    } else if (starts({0xfe, 0xff})) {
        result.encoding = StreamReaderEncoding::Utf16BE;
        result.bom_bytes = 2;
    } else if (starts({0xef, 0xbb, 0xbf})) {
        result.encoding = StreamReaderEncoding::Utf8;
        result.bom_bytes = 3;
    }
    result.text = decode_unicode_text(bytes.subspan(result.bom_bytes), result.encoding);
    return result;
}
std::u16string decode_unicode_text(std::span<const std::uint8_t> bytes, StreamReaderEncoding encoding) {
    if (encoding == StreamReaderEncoding::Utf8)
        return utf16_replacing(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    std::u16string result;
    const bool little = encoding == StreamReaderEncoding::Utf16LE || encoding == StreamReaderEncoding::Utf32LE;
    if (encoding == StreamReaderEncoding::Utf16LE || encoding == StreamReaderEncoding::Utf16BE) {
        char16_t pending = 0;
        std::size_t index = 0;
        for (; index + 1 < bytes.size(); index += 2) {
            char16_t value = static_cast<char16_t>(
                little ? std::uint32_t(bytes[index]) | (std::uint32_t(bytes[index + 1]) << 8)
                       : (std::uint32_t(bytes[index]) << 8) | bytes[index + 1]);
            if (pending) {
                if (value >= 0xdc00 && value <= 0xdfff) {
                    result += pending;
                    result += value;
                    pending = 0;
                    continue;
                }
                result += u'\ufffd';
                pending = 0;
            }
            if (value >= 0xd800 && value <= 0xdbff)
                pending = value;
            else if (value >= 0xdc00 && value <= 0xdfff)
                result += u'\ufffd';
            else
                result += value;
        }
        if (pending)
            result += u'\ufffd';
        if (index < bytes.size())
            result += u'\ufffd';
        return result;
    }
    std::size_t index = 0;
    for (; index + 3 < bytes.size(); index += 4) {
        std::uint32_t value = 0;
        for (std::size_t n = 0; n < 4; ++n)
            value |= std::uint32_t(bytes[index + n]) << ((little ? n : 3 - n) * 8);
        append(result, value);
    }
    if (index < bytes.size())
        result += u'\ufffd';
    return result;
}
std::vector<std::u16string> stream_reader_lines(std::u16string_view text) {
    std::vector<std::u16string> lines;
    std::size_t start = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != u'\r' && text[index] != u'\n')
            continue;
        lines.emplace_back(text.substr(start, index - start));
        if (text[index] == u'\r' && index + 1 < text.size() && text[index + 1] == u'\n')
            ++index;
        start = index + 1;
    }
    if (start < text.size())
        lines.emplace_back(text.substr(start));
    return lines;
}
StreamReaderText read_stream_reader_text(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 4096> buffer{};
#ifdef _WIN32
    auto normalized = windows_stream_path(path);
    struct Handle {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~Handle() {
            if (value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    } file;
    file.value = CreateFileW(normalized.io.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | SECURITY_SQOS_PRESENT |
                                 SECURITY_ANONYMOUS,
                             nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        auto code = GetLastError();
        if (code == ERROR_PATH_NOT_FOUND && normalized.full.size() == root_length(normalized.full))
            code = ERROR_ACCESS_DENIED;
        throw StreamReaderTextError(StreamReaderTextFailure::Open, path,
                                    std::error_code(static_cast<int>(code), std::system_category()));
    }
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
            auto code = GetLastError();
            throw StreamReaderTextError(StreamReaderTextFailure::Read, path,
                                        std::error_code(static_cast<int>(code), std::system_category()));
        }
        if (!count)
            break;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
    }
#else
    auto native = path.native();
    if (native.empty() || native.find(typename std::filesystem::path::value_type{}) != native.npos)
        throw StreamReaderTextError(StreamReaderTextFailure::Open, path,
                                    std::make_error_code(std::errc::invalid_argument));
    struct Handle {
        int value = -1;
        ~Handle() {
            if (value >= 0)
                ::close(value);
        }
    } file;
    file.value = ::open(path.c_str(), O_RDONLY);
    if (file.value < 0)
        throw StreamReaderTextError(StreamReaderTextFailure::Open, path,
                                    std::error_code(errno, std::generic_category()));
    for (;;) {
        auto count = ::read(file.value, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR)
                continue;
            throw StreamReaderTextError(StreamReaderTextFailure::Read, path,
                                        std::error_code(errno, std::generic_category()));
        }
        if (!count)
            break;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
    }
#endif
    return decode_stream_reader_text(bytes);
}
}

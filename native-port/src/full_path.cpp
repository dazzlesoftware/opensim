// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/full_path.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>
#include <limits>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif
namespace opensim::framework {
namespace {
#ifdef _WIN32
bool extended(std::u16string_view value) {
    return value.size() >= 4 && value[0] == u'\\' && (value[1] == u'\\' || value[1] == u'?') &&
           value[2] == u'?' && value[3] == u'\\';
}
[[noreturn]] void windows_error(DWORD code, const std::u16string& path) {
    if (code == ERROR_FILENAME_EXCED_RANGE)
        throw FullPathError(FullPathFailure::PathTooLong,
                            "The path '" + utf8(path) +
                                "' is too long, or a component of the specified path is too long.",
                            {}, static_cast<std::uint32_t>(code));
    throw FullPathError(FullPathFailure::Io,
                        std::system_category().message(static_cast<int>(code)) +
                            (path.empty() ? std::string{} : " : '" + utf8(path) + "'"),
                        {}, static_cast<std::uint32_t>(code));
}
std::u16string normalize(const std::u16string& path) {
    std::vector<wchar_t> buffer(261);
    for (;;) {
        auto length = GetFullPathNameW(reinterpret_cast<const wchar_t*>(path.c_str()),
                                       static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (!length) {
            auto code = GetLastError();
            windows_error(code ? code : ERROR_BAD_PATHNAME, path);
        }
        if (length > 32767)
            windows_error(ERROR_FILENAME_EXCED_RANGE, path);
        if (length < buffer.size())
            return {reinterpret_cast<const char16_t*>(buffer.data()), length};
        buffer.resize(static_cast<std::size_t>(length) + 1);
    }
}
std::optional<std::u16string> long_name(const std::u16string& value) {
    auto requested = value;
    bool prefix = false, unc = false;
    if (value.size() > 259 && !extended(value) && !value.starts_with(u"\\\\.\\")) {
        prefix = true;
        if (value.starts_with(u"\\\\")) {
            unc = true;
            requested = u"\\\\?\\UNC\\" + value.substr(2);
        } else
            requested = u"\\\\?\\" + value;
    }
    std::vector<wchar_t> buffer(261);
    for (;;) {
        auto length = GetLongPathNameW(reinterpret_cast<const wchar_t*>(requested.c_str()), buffer.data(),
                                       static_cast<DWORD>(buffer.size()));
        if (!length)
            return {};
        if (length < buffer.size()) {
            std::u16string result(reinterpret_cast<const char16_t*>(buffer.data()), length);
            if (prefix) {
                if (unc && result.starts_with(u"\\\\?\\UNC\\"))
                    result = u"\\\\" + result.substr(8);
                else if (!unc && result.starts_with(u"\\\\?\\"))
                    result = result.substr(4);
            }
            return result;
        }
        if (length > 32767)
            return {};
        buffer.resize(static_cast<std::size_t>(length) + 1);
    }
}
std::u16string expand(const std::u16string& path) {
    std::size_t end = path.size();
    for (;;) {
        if (auto expanded = long_name(path.substr(0, end)))
            return *expanded + path.substr(end);
        if (!end)
            break;
        auto separator = path.rfind(u'\\', end - 1);
        if (separator == std::u16string::npos || separator < 2)
            break;
        if (separator == 2 && path[1] == u':') {
            if (auto root = long_name(path.substr(0, 3)))
                return *root + path.substr(3);
            break;
        }
        end = separator;
    }
    return path;
}
#else
UnixPathError unix_error(int code) {
    switch (code) {
    case ENOENT:
        return UnixPathError::NoEntry;
    case ENOTDIR:
        return UnixPathError::NotDirectory;
    case EACCES:
        return UnixPathError::AccessDenied;
    case EBADF:
        return UnixPathError::BadDescriptor;
    case EPERM:
        return UnixPathError::NotPermitted;
    case ENAMETOOLONG:
        return UnixPathError::NameTooLong;
    case EWOULDBLOCK:
        return UnixPathError::WouldBlock;
    case ECANCELED:
        return UnixPathError::Canceled;
    case EFBIG:
        return UnixPathError::FileTooLarge;
    case EEXIST:
        return UnixPathError::AlreadyExists;
    default:
        return UnixPathError::Other;
    }
}
UnixCwdResult native_unix_cwd() {
    std::vector<char> buffer(256);
    for (;;) {
        if (::getcwd(buffer.data(), buffer.size()))
            return {std::string(buffer.data())};
        auto code = errno;
        if (code != ERANGE)
            return {{}, unix_error(code), std::generic_category().message(code), code};
        if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 2))
            throw std::overflow_error("Arithmetic operation resulted in an overflow.");
        buffer.resize(buffer.size() * 2);
    }
}
std::u16string normalize(const std::u16string& text) {
    return utf16(get_full_path_unix(utf8(text), native_unix_cwd));
}
std::u16string expand(const std::u16string& text) {
    return text;
}

#endif
}
namespace {
[[noreturn]] void throw_unix_cwd(const UnixCwdResult& result) {
    switch (result.error) {
    case UnixPathError::NoEntry:
        throw FullPathError(FullPathFailure::MissingFile, "Unable to find the specified file.");
    case UnixPathError::NotDirectory:
        throw FullPathError(FullPathFailure::MissingDirectory, "Could not find a part of the path.");
    case UnixPathError::AccessDenied:
    case UnixPathError::BadDescriptor:
    case UnixPathError::NotPermitted:
        throw FullPathError(FullPathFailure::Unauthorized, "Access to the path is denied.");
    case UnixPathError::NameTooLong:
        throw FullPathError(
            FullPathFailure::PathTooLong,
            "The specified file name or path is too long, or a component of the specified path is too long.");
    case UnixPathError::WouldBlock:
        throw FullPathError(
            FullPathFailure::Io,
            "The process cannot access the file because it is being used by another process.");
    case UnixPathError::Canceled:
        throw FullPathError(FullPathFailure::OperationCanceled, "The operation was canceled.");
    case UnixPathError::FileTooLarge:
        throw FullPathError(FullPathFailure::ArgumentOutOfRange,
                            "Specified file length was too large for the file system. (Parameter 'value')",
                            "value");
    default:
        throw FullPathError(FullPathFailure::Io, result.system_message);
    }
}
}
std::string get_full_path_unix(const std::optional<std::string>& input, UnixCwdProvider cwd) {
    if (!input)
        throw FullPathError(FullPathFailure::ArgumentNull, "Value cannot be null. (Parameter 'path')",
                            "path");
    auto text = utf16_replacing(*input);
    if (text.empty())
        throw FullPathError(FullPathFailure::Argument,
                            "The value cannot be an empty string. (Parameter 'path')", "path");
    if (text.find(u'\0') != text.npos)
        throw FullPathError(FullPathFailure::Argument, "Null character in path. (Parameter 'path')", "path");
    auto path = utf8(text);
    if (path.front() != '/') {
        if (!cwd)
            throw std::invalid_argument("Unix full path requires a cwd provider");
        auto current = cwd();
        if (current.error != UnixPathError::Success)
            throw_unix_cwd(current);
        auto length = current.bytes.find('\0');
        std::string base = utf8(utf16_replacing(std::string_view(current.bytes).substr(0, length)));
        path = base + (base.ends_with('/') || base.empty() ? "" : "/") + path;
    }
    bool trailing = path.ends_with('/');
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        auto segment = path.substr(start, end - start);
        if (segment == "..") {
            if (!segments.empty())
                segments.pop_back();
        } else if (!segment.empty() && segment != ".")
            segments.push_back(std::move(segment));
        start = end + 1;
    }
    std::string result = "/";
    for (auto& segment : segments) {
        if (result.size() > 1)
            result += '/';
        result += segment;
    }
    if (trailing && result.size() > 1)
        result += '/';
    return result;
}
std::shared_ptr<FullPathBackend> native_full_path_backend() {
    auto result = std::make_shared<FullPathBackend>();
    result->normalize = normalize;
    result->expand_short = expand;
    return result;
}
std::string get_full_path(const std::optional<std::string>& path, std::shared_ptr<FullPathBackend> backend) {
    if (!path)
        throw FullPathError(FullPathFailure::ArgumentNull, "Value cannot be null. (Parameter 'path')",
                            "path");
    auto value = utf16_replacing(*path);
    bool empty = value.empty();
#ifdef _WIN32
    empty = empty || std::all_of(value.begin(), value.end(), [](char16_t c) { return c == u' '; });
#endif
    if (empty) {
#ifdef _WIN32
        throw FullPathError(FullPathFailure::Argument, "The path is empty. (Parameter 'path')", "path");
#else
        throw FullPathError(FullPathFailure::Argument,
                            "The value cannot be an empty string. (Parameter 'path')", "path");
#endif
    }
    if (value.find(u'\0') != value.npos)
        throw FullPathError(FullPathFailure::Argument, "Null character in path. (Parameter 'path')", "path");
#ifdef _WIN32
    if (extended(value))
        return utf8(value);
#endif
    if (!backend)
        backend = native_full_path_backend();
    if (!backend->normalize || !backend->expand_short)
        throw std::invalid_argument("Incomplete full-path providers");
    auto result = backend->normalize(value);
#ifdef _WIN32
    if (result.find(u'~') != result.npos)
        result = backend->expand_short(result);
#endif
    return utf8(result);
}
}

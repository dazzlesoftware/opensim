// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_console_files.hpp"
#include "opensim/framework/full_path.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#else
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#endif
namespace opensim::services {
FlotsamConsoleFilesError::FlotsamConsoleFilesError(Kind kind, const std::string& message, std::uint32_t code,
                                                   std::optional<std::string> parameter)
    : runtime_error(message), kind(kind), code(code), parameter(std::move(parameter)), message(message) {
}
const char* FlotsamConsoleFilesError::source_type() const noexcept {
    switch (kind) {
    case Kind::ArgumentNull:
        return "System.ArgumentNullException";
    case Kind::Argument:
        return "System.ArgumentException";
    case Kind::ArgumentOutOfRange:
        return "System.ArgumentOutOfRangeException";
    case Kind::PathTooLong:
        return "System.IO.PathTooLongException";
    case Kind::Unauthorized:
        return "System.UnauthorizedAccessException";
    case Kind::DirectoryNotFound:
        return "System.IO.DirectoryNotFoundException";
    case Kind::FileNotFound:
        return "System.IO.FileNotFoundException";
    case Kind::OperationCanceled:
        return "System.OperationCanceledException";
    default:
        return "System.IO.IOException";
    }
}
namespace {
using Error = FlotsamConsoleFilesError;
using Kind = Error::Kind;
using Date = framework::AssetDateTime;
using Text = std::optional<std::string>;
[[maybe_unused]] constexpr std::int64_t epoch = 621355968000000000ll, file_epoch = 504911232000000000ll,
                                        maximum = 3155378975999999999ll;
#ifdef _WIN32
[[noreturn]] void error(DWORD code, const std::string& path);
#endif
std::string full_path(const Text& path) {
    try {
        return framework::get_full_path(path);
    } catch (const framework::FullPathError& e) {
        switch (e.kind) {
        case framework::FullPathFailure::ArgumentNull:
            throw Error(Kind::ArgumentNull, e.what(), 0x80004003u, e.parameter);
        case framework::FullPathFailure::Argument:
            throw Error(Kind::Argument, e.what(), 0x80070057u, e.parameter);
        case framework::FullPathFailure::PathTooLong:
            throw Error(Kind::PathTooLong,
                        "The path '" + path.value_or("") +
                            "' is too long, or a component of the specified path is too long.",
                        0x800700ceu);
        default:
#ifdef _WIN32
            if (e.win32_code)
                error(*e.win32_code, path.value_or(""));
#endif
            throw Error(Kind::Io, e.what(), 0x80131620u, e.parameter);
        }
    }
}
bool match(std::u16string_view name) {
    if (name.size() < 4 || name[name.size() - 4] != u'.')
        return false;
    auto suffix = name.substr(name.size() - 3);
    constexpr std::u16string_view lower = u"fac";
    for (std::size_t i = 0; i < 3; ++i) {
        auto c = suffix[i];
#if defined(_WIN32) || defined(__APPLE__)
        if (c >= u'A' && c <= u'Z')
            c += u'a' - u'A';
#endif
        if (c != lower[i])
            return false;
    }
    return true;
}
std::string join(const std::string& root, std::u16string_view name) {
    char separator = '/';
    bool ended = !root.empty() && root.back() == '/';
#ifdef _WIN32
    separator = '\\';
    ended = ended || (!root.empty() && root.back() == '\\');
#endif
    return root + (ended ? "" : std::string(1, separator)) + framework::utf8(name);
}
#ifdef _WIN32
std::wstring wide(std::string_view value) {
    auto text = framework::utf16_replacing(value);
    return {reinterpret_cast<const wchar_t*>(text.data()), text.size()};
}
std::wstring io_path(const std::wstring& path) {
    if (path.size() >= 4 &&
        ((path[0] == L'\\' && path[1] == L'\\' && (path[2] == L'?' || path[2] == L'.') && path[3] == L'\\') ||
         (path[0] == L'\\' && path[1] == L'?' && path[2] == L'?' && path[3] == L'\\')))
        return path;
    if (path.size() < 260)
        return path;
    if (path.starts_with(L"\\\\"))
        return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}
std::string native_message(DWORD code) {
    wchar_t* buffer = nullptr;
    auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (!length)
        return "Unknown error (0x" + std::to_string(code) + ")";
    std::u16string text(reinterpret_cast<char16_t*>(buffer), length);
    LocalFree(buffer);
    while (!text.empty() && (text.back() == u'\r' || text.back() == u'\n' || text.back() == u' '))
        text.pop_back();
    return framework::utf8(text);
}
[[noreturn]] void error(DWORD code, const std::string& path) {
    auto hr = (code & 0xffff0000u) ? code : 0x80070000u | code;
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
        throw Error(Kind::FileNotFound, "Could not find file '" + path + "'.", 0x80070002u);
    case ERROR_PATH_NOT_FOUND:
        throw Error(Kind::DirectoryNotFound, "Could not find a part of the path '" + path + "'.",
                    0x80070003u);
    case ERROR_ACCESS_DENIED:
        throw Error(Kind::Unauthorized, "Access to the path '" + path + "' is denied.", 0x80070005u);
    case ERROR_SHARING_VIOLATION:
        throw Error(Kind::Io,
                    "The process cannot access the file '" + path +
                        "' because it is being used by another process.",
                    hr);
    case ERROR_FILENAME_EXCED_RANGE:
        throw Error(Kind::PathTooLong,
                    "The path '" + path + "' is too long, or a component of the specified path is too long.",
                    0x800700ceu);
    case ERROR_OPERATION_ABORTED:
        throw Error(Kind::OperationCanceled, "The operation was canceled.", 0x8013153bu);
    default:
        throw Error(Kind::Io, native_message(code) + " : '" + path + "'", hr);
    }
}
std::size_t root_length(std::wstring_view path) {
    auto separator = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    auto length = path.size();
    std::size_t i = 0;
    bool extended = length >= 4 && path[0] == L'\\' && (path[1] == L'\\' || path[1] == L'?') &&
                    path[2] == L'?' && path[3] == L'\\';
    bool device = extended || (length >= 4 && separator(path[0]) && separator(path[1]) &&
                               (path[2] == L'.' || path[2] == L'?') && separator(path[3]));
    bool unc =
        device && length >= 8 && separator(path[7]) && path[4] == L'U' && path[5] == L'N' && path[6] == L'C';
    if ((!device || unc) && length && separator(path[0])) {
        if (unc || (length > 1 && separator(path[1]))) {
            i = unc ? 8 : 2;
            int remaining = 2;
            for (; i < length; ++i)
                if (separator(path[i]) && --remaining <= 0)
                    break;
        } else
            i = 1;
    } else if (device) {
        for (i = 4; i < length && !separator(path[i]); ++i) {
        }
        if (i < length && i > 4 && separator(path[i]))
            ++i;
    } else if (length >= 2 && path[1] == L':' &&
               ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))) {
        i = 2;
        if (length > 2 && separator(path[2]))
            ++i;
    }
    return i;
}
std::wstring trim(std::wstring path) {
    if (path.size() > root_length(path) && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE && value)
            CloseHandle(value);
    }
};
struct FullInfo {
    ULONG next, index;
    LARGE_INTEGER creation, access, write, change, size, allocation;
    ULONG attributes, name_length, ea_size;
    WCHAR name[1];
};
static_assert(offsetof(FullInfo, name) == 68);
using Query = LONG(NTAPI*)(HANDLE, HANDLE, void*, void*, IO_STATUS_BLOCK*, void*, ULONG, ULONG, BOOLEAN,
                           void*, BOOLEAN);
using Convert = ULONG(NTAPI*)(LONG);
bool unreachable(DWORD code) {
    switch (code) {
    case 2:
    case 3:
    case 6:
    case 21:
    case 53:
    case 65:
    case 67:
    case 87:
    case 123:
    case 161:
    case 206:
    case 1231:
        return true;
    default:
        return false;
    }
}
#else
[[noreturn]] void error(int code, const std::string& path) {
    if (code == ENOENT || code == ENOTDIR)
        throw Error(Kind::DirectoryNotFound, "Could not find a part of the path '" + path + "'.",
                    0x80070003u);
    if (code == EACCES || code == EPERM)
        throw Error(Kind::Unauthorized, "Access to the path '" + path + "' is denied.", 0x80070005u);
    throw Error(Kind::Io, std::generic_category().message(code) + " : '" + path + "'", 0x80131620u);
}

#endif
}
std::vector<std::optional<std::string>> native_flotsam_status_files(const Text& root) {
    if (root && root->find('\0') != root->npos)
        throw Error(Kind::Argument, "Null character in path. (Parameter '" + *root + "')", 0x80070057u,
                    *root);
    auto full = full_path(root);
    std::vector<Text> out;
#ifdef _WIN32
    auto directory = trim(wide(full));
    auto io = io_path(directory);
    Handle handle{CreateFileW(io.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE) {
        auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND)
            code = ERROR_PATH_NOT_FOUND;
        error(code, framework::utf8(std::u16string_view(reinterpret_cast<const char16_t*>(directory.data()),
                                                        directory.size())));
    }
    auto module = GetModuleHandleW(L"ntdll.dll");
    auto query = reinterpret_cast<Query>(GetProcAddress(module, "NtQueryDirectoryFile"));
    auto convert = reinterpret_cast<Convert>(GetProcAddress(module, "RtlNtStatusToDosError"));
    if (!query || !convert)
        throw Error(Kind::Io, "Native directory enumeration unavailable", 0x80131620u);
    alignas(void*) std::array<std::byte, 4096> buffer{};
    for (;;) {
        IO_STATUS_BLOCK status{};
        auto code = query(handle.value, nullptr, nullptr, nullptr, &status, buffer.data(),
                          static_cast<ULONG>(buffer.size()), 2, FALSE, nullptr, FALSE);
        if (static_cast<ULONG>(code) == 0x80000006u || static_cast<ULONG>(code) == 0xc000000fu)
            break;
        if (code)
            error(convert(code), framework::utf8(std::u16string_view(
                                     reinterpret_cast<const char16_t*>(directory.data()), directory.size())));
        std::size_t offset = 0;
        for (;;) {
            auto entry = reinterpret_cast<const FullInfo*>(buffer.data() + offset);
            if (!(entry->attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                auto name = std::u16string_view(reinterpret_cast<const char16_t*>(entry->name),
                                                entry->name_length / 2);
                if (match(name))
                    out.emplace_back(join(*root, name));
            }
            if (!entry->next)
                break;
            offset += entry->next;
        }
    }
#else
    auto directory = ([&]() {
        decltype(::opendir(full.c_str())) result;
        do {
            result = ::opendir(full.c_str());
        } while (!result && errno == EINTR);
        return result;
    }());
    if (!directory)
        error(errno, full);
    try {
        errno = 0;
        while (auto entry = ::readdir(directory)) {
            std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;
            auto absolute = full + (full.ends_with('/') ? "" : "/") + name;
            struct stat st{};
            bool is_directory = ([&]() {
                                    decltype(::stat(absolute.c_str(), &st)) result;
                                    do {
                                        result = ::stat(absolute.c_str(), &st);
                                    } while (result < 0 && errno == EINTR);
                                    return result;
                                }()) == 0 &&
                                S_ISDIR(st.st_mode);
            if (!is_directory && match(framework::utf16_replacing(name)))
                out.emplace_back(join(*root, framework::utf16_replacing(name)));
            errno = 0;
        }
        if (errno)
            error(errno, full);
    } catch (...) {
        ::closedir(directory);
        throw;
    }
    ::closedir(directory);
#endif
    return out;
}
framework::AssetDateTime native_flotsam_last_write(const Text& path) {
    return native_flotsam_last_write(path, framework::shared_native_invariant_datetime_context());
}
framework::AssetDateTime
native_flotsam_last_write(const Text& path,
                          const std::shared_ptr<framework::NativeInvariantDateTimeContext>& datetime) {
    auto full = full_path(path);
    if (!datetime)
        throw std::invalid_argument("Null native datetime context");
    [[maybe_unused]] auto local = [&](std::int64_t ticks) { return datetime->to_local_time(Date(ticks, 1)); };
#ifdef _WIN32
    auto native = trim(wide(full));
    auto io = io_path(native);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    DWORD code = 0;
    if (!GetFileAttributesExW(io.c_str(), GetFileExInfoStandard, &data)) {
        code = GetLastError();
        if (!unreachable(code)) {
            WIN32_FIND_DATAW found{};
            auto handle = FindFirstFileW(io.c_str(), &found);
            if (handle == INVALID_HANDLE_VALUE)
                code = GetLastError();
            else {
                FindClose(handle);
                code = 0;
                data.ftLastWriteTime = found.ftLastWriteTime;
            }
        }
    }
    if (code && code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND && code != ERROR_NOT_READY)
        error(code, full);
    auto file =
        (std::uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    if (file > static_cast<std::uint64_t>(maximum - file_epoch))
        throw Error(Kind::ArgumentOutOfRange, "Not a valid Win32 FileTime. (Parameter 'fileTime')",
                    0x80131502u, "fileTime");
    return datetime->to_local_time(datetime->from_file_time_utc(static_cast<std::int64_t>(file)));
#else
    auto metadata_path = full;
    while (metadata_path.size() > 1 && metadata_path.back() == '/')
        metadata_path.pop_back();
    struct stat st{};
    if (::lstat(metadata_path.c_str(), &st) < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return local(file_epoch);
        error(errno, full);
    }
#ifdef __APPLE__
    auto seconds = st.st_mtimespec.tv_sec;
    auto nanos = st.st_mtimespec.tv_nsec;
#else
    auto seconds = st.st_mtim.tv_sec;
    auto nanos = st.st_mtim.tv_nsec;
#endif
    if (seconds < -62135596800ll || seconds > 253402300799ll)
        throw Error(Kind::ArgumentOutOfRange, "The file write time is outside the DateTime range.",
                    0x80131502u);
    auto ticks = epoch + static_cast<std::int64_t>(seconds) * 10000000ll + nanos / 100;
    return local(ticks);
#endif
}
void bind_flotsam_console_files(FlotsamConsoleBackend& backend,
                                std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime) {
    if (!datetime)
        datetime = framework::shared_native_invariant_datetime_context();
    backend.status_files = [](const Text& root, const std::string& pattern) {
        if (pattern != "*.fac")
            throw std::invalid_argument("Status provider requires '*.fac'");
        return native_flotsam_status_files(root);
    };
    backend.last_write = [datetime](const Text& path) { return native_flotsam_last_write(path, datetime); };
}
}

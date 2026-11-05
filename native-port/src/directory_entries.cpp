// Derived from pinned .NET Runtime v8.0.31 default filesystem enumeration.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// ../third_party/dotnet_collation/LICENSE.
#include "opensim/framework/directory_entries.hpp"
#include "opensim/framework/full_path.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <bit>
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
namespace opensim::framework {
DirectoryEntriesError::DirectoryEntriesError(std::string t, std::int32_t h, std::u16string m,
                                             std::optional<std::u16string> p)
    : runtime_error(utf8(m)), source_type(std::move(t)), hresult(h), message(std::move(m)),
      parameter(std::move(p)) {
}
namespace {
using Text = DirectoryEntriesText;
using Platform = DirectoryEntriesPlatform;
Text text(std::u16string value) {
    return std::make_shared<const std::u16string>(std::move(value));
}
[[noreturn]] void fail(std::string type, std::uint32_t hr, std::u16string message,
                       std::optional<std::u16string> parameter = {}) {
    throw DirectoryEntriesError(std::move(type), std::bit_cast<std::int32_t>(hr), std::move(message),
                                std::move(parameter));
}
void argnull(std::u16string name) {
    fail("System.ArgumentNullException", 0x80004003u, u"Value cannot be null. (Parameter '" + name + u"')",
         name);
}
bool sep(char16_t c, Platform p) {
    return c == u'/' || (p == Platform::Windows && c == u'\\');
}
std::u16string join(std::u16string_view a, std::u16string_view b, Platform p) {
    if (a.empty())
        return std::u16string(b);
    if (b.empty())
        return std::u16string(a);
    std::u16string out(a);
    if (!sep(a.back(), p) && !sep(b.front(), p))
        out += p == Platform::Windows ? u'\\' : u'/';
    out += b;
    return out;
}
bool drive(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}
bool rooted(std::u16string_view path, Platform p) {
    return !path.empty() && (sep(path[0], p) || (p == Platform::Windows && path.size() >= 2 &&
                                                 drive(path[0]) && path[1] == u':'));
}
std::u16string replace(std::u16string value, char16_t find, std::u16string_view replacement) {
    std::u16string result;
    for (auto c : value) {
        if (c == find)
            result += replacement;
        else
            result += c;
    }
    return result;
}
constexpr Platform native_platform() {
#ifdef _WIN32
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Linux;
#endif
}
std::size_t root_length(std::u16string_view p, Platform platform) {
    if (platform != Platform::Windows)
        return !p.empty() && p[0] == u'/' ? 1 : 0;
    auto n = p.size();
    std::size_t i = 0;
    bool extended =
        n >= 4 && p[0] == u'\\' && (p[1] == u'\\' || p[1] == u'?') && p[2] == u'?' && p[3] == u'\\';
    bool device = extended || (n >= 4 && sep(p[0], platform) && sep(p[1], platform) &&
                               (p[2] == u'.' || p[2] == u'?') && sep(p[3], platform));
    bool unc = device && n >= 8 && sep(p[7], platform) && p[4] == u'U' && p[5] == u'N' && p[6] == u'C';
    if ((!device || unc) && n && sep(p[0], platform)) {
        if (unc || (n > 1 && sep(p[1], platform))) {
            i = unc ? 8 : 2;
            int remaining = 2;
            for (; i < n; ++i)
                if (sep(p[i], platform) && --remaining <= 0)
                    break;
        } else
            i = 1;
    } else if (device) {
        for (i = 4; i < n && !sep(p[i], platform); ++i) {
        }
        if (i < n && i > 4 && sep(p[i], platform))
            ++i;
    } else if (n >= 2 && p[1] == u':' && drive(p[0])) {
        i = 2;
        if (n > 2 && sep(p[2], platform))
            ++i;
    }
    return i;
}
std::u16string trim(std::u16string path, Platform platform) {
    if (path.size() > root_length(path, platform) && sep(path.back(), platform))
        path.pop_back();
    return path;
}
std::u16string number(std::uint32_t value) {
    auto n = std::to_string(value);
    return {n.begin(), n.end()};
}
}
NormalizedDirectorySearch normalize_directory_search(Text directory, Text expression, Platform platform) {
    if (!directory)
        argnull(u"path");
    if (!expression)
        argnull(u"searchPattern");
    if (rooted(*expression, platform))
        fail("System.ArgumentException", 0x80070057u,
             u"Second path fragment must not be a drive or UNC name. (Parameter 'expression')",
             u"expression");
    if (expression->find(u'\0') != expression->npos)
        fail("System.ArgumentException", 0x80070057u,
             u"Null character in path. (Parameter '" + *expression + u"')", *expression);
    if (directory->find(u'\0') != directory->npos)
        fail("System.ArgumentException", 0x80070057u,
             u"Null character in path. (Parameter '" + *directory + u"')", *directory);
    bool unchanged = true;
    std::size_t end = expression->size();
    while (end > 0 && !sep((*expression)[--end], platform)) {
    }
    while (end > 0 && sep((*expression)[end - 1], platform))
        --end;
    if (end) {
        directory = text(join(*directory, std::u16string_view(*expression).substr(0, end), platform));
        expression = text(expression->substr(end + 1));
        unchanged = false;
    }
    if (*expression != u"*") {
        if (expression->empty() || *expression == u"." || *expression == u"*.*")
            expression = file_system_name::star_literal();
        else {
            if (platform != Platform::Windows && expression->find_first_of(u"\\\"<>") != expression->npos) {
                auto value = replace(*expression, u'\\', u"\\\\");
                value = replace(std::move(value), u'\"', u"\\\"");
                value = replace(std::move(value), u'>', u"\\>");
                value = replace(std::move(value), u'<', u"\\<");
                expression = text(std::move(value));
            }
            expression = file_system_name::translate_win32_expression(expression);
        }
    }
    return {std::move(directory), std::move(expression), unchanged};
}
namespace {
#ifdef _WIN32
bool extended(std::u16string_view p) {
    return p.size() >= 4 && p[0] == u'\\' && (p[1] == u'\\' || p[1] == u'?') && p[2] == u'?' && p[3] == u'\\';
}
bool device(std::u16string_view p) {
    return extended(p) || (p.size() >= 4 && sep(p[0], Platform::Windows) && sep(p[1], Platform::Windows) &&
                           (p[2] == u'.' || p[2] == u'?') && sep(p[3], Platform::Windows));
}
std::u16string io_path(const std::u16string& p) {
    if (device(p) || (p.size() < 260 && !p.ends_with(u'.') && !p.ends_with(u' ')))
        return p;
    if (p.starts_with(u"\\\\"))
        return u"\\\\?\\UNC\\" + p.substr(2);
    return u"\\\\?\\" + p;
}
std::u16string native_message(DWORD code) {
    wchar_t* b = nullptr;
    auto n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, code, 0, reinterpret_cast<wchar_t*>(&b), 0, nullptr);
    if (!n)
        return u"Unknown error (0x" + number(code) + u")";
    std::u16string result(reinterpret_cast<char16_t*>(b), n);
    LocalFree(b);
    while (!result.empty() && (result.back() == u'\r' || result.back() == u'\n' || result.back() == u' '))
        result.pop_back();
    return result;
}
[[noreturn]] void os_error(DWORD code, const std::u16string& path) {
    auto hr = (code & 0xffff0000u) ? code : 0x80070000u | code;
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
        fail("System.IO.FileNotFoundException", 0x80070002u, u"Could not find file '" + path + u"'.");
    case ERROR_PATH_NOT_FOUND:
        fail("System.IO.DirectoryNotFoundException", 0x80070003u,
             u"Could not find a part of the path '" + path + u"'.");
    case ERROR_ACCESS_DENIED:
        fail("System.UnauthorizedAccessException", 0x80070005u,
             u"Access to the path '" + path + u"' is denied.");
    case ERROR_SHARING_VIOLATION:
        fail("System.IO.IOException", hr,
             u"The process cannot access the file '" + path +
                 u"' because it is being used by another process.");
    case ERROR_FILENAME_EXCED_RANGE:
        fail("System.IO.PathTooLongException", 0x800700ceu,
             u"The path '" + path + u"' is too long, or a component of the specified path is too long.");
    case ERROR_OPERATION_ABORTED:
        fail("System.OperationCanceledException", 0x8013153bu, u"The operation was canceled.");
    default:
        fail("System.IO.IOException", hr,
             native_message(code) + (path.empty() ? u"" : u" : '" + path + u"'"));
    }
}
#else
[[noreturn]] void os_error(int code, const std::u16string& path) {
    switch (code) {
    case ENOENT:
    case ENOTDIR:
        fail("System.IO.DirectoryNotFoundException", 0x80070003u,
             u"Could not find a part of the path '" + path + u"'.");
    case EACCES:
    case EBADF:
    case EPERM:
        fail("System.UnauthorizedAccessException", 0x80070005u,
             u"Access to the path '" + path + u"' is denied.");
    case ENAMETOOLONG:
        fail("System.IO.PathTooLongException", 0x800700ceu,
             u"The path '" + path + u"' is too long, or a component of the specified path is too long.");
    case EWOULDBLOCK:
        fail("System.IO.IOException", static_cast<std::uint32_t>(code),
             u"The process cannot access the file '" + path +
                 u"' because it is being used by another process.");
    case ECANCELED:
        fail("System.OperationCanceledException", 0x8013153bu, u"The operation was canceled.");
    case EFBIG:
        fail("System.ArgumentOutOfRangeException", 0x80131502u,
             u"Specified file length was too large for the file system. (Parameter 'value')", u"value");
    default:
        fail("System.IO.IOException", static_cast<std::uint32_t>(code),
             utf16_replacing(std::generic_category().message(code)) + u" : '" + path + u"'");
    }
}
#endif
[[noreturn]] void full_error(const FullPathError& e, const Text& original) {
#ifdef _WIN32
    if (e.win32_code)
        os_error(*e.win32_code, *original);
#endif
    std::string type = "System.IO.IOException";
    std::uint32_t hr = 0x80131620u;
    switch (e.kind) {
    case FullPathFailure::ArgumentNull:
        type = "System.ArgumentNullException";
        hr = 0x80004003u;
        break;
    case FullPathFailure::Argument:
        type = "System.ArgumentException";
        hr = 0x80070057u;
        break;
    case FullPathFailure::MissingFile:
        type = "System.IO.FileNotFoundException";
        hr = 0x80070002u;
        break;
    case FullPathFailure::MissingDirectory:
        type = "System.IO.DirectoryNotFoundException";
        hr = 0x80070003u;
        break;
    case FullPathFailure::Unauthorized:
        type = "System.UnauthorizedAccessException";
        hr = 0x80070005u;
        break;
    case FullPathFailure::PathTooLong:
        type = "System.IO.PathTooLongException";
        hr = 0x800700ceu;
        break;
    case FullPathFailure::OperationCanceled:
        type = "System.OperationCanceledException";
        hr = 0x8013153bu;
        break;
    case FullPathFailure::ArgumentOutOfRange:
        type = "System.ArgumentOutOfRangeException";
        hr = 0x80131502u;
        break;
    default:
        break;
    }
    fail(type, hr, utf16_replacing(e.what()),
         e.parameter ? std::optional(utf16_replacing(*e.parameter)) : std::nullopt);
}
Text native_full_path(Text p) {
#ifdef _WIN32
    if (p->empty() || std::all_of(p->begin(), p->end(), [](auto c) { return c == u' '; }))
        fail("System.ArgumentException", 0x80070057u, u"The path is empty. (Parameter 'path')", u"path");
    if (extended(*p))
        return p;
    try {
        auto backend = native_full_path_backend();
        auto full = backend->normalize(*p);
        if (full.find(u'~') != full.npos)
            full = backend->expand_short(full);
        return text(std::move(full));
    } catch (const FullPathError& e) {
        full_error(e, p);
    }
#else
    if (p->empty())
        fail("System.ArgumentException", 0x80070057u,
             u"The value cannot be an empty string. (Parameter 'path')", u"path");
    std::u16string value = *p;
    if (value.front() != u'/') {
        std::u16string cwd;
        try {
            cwd = utf16_replacing(get_full_path(std::string(".")));
        } catch (const FullPathError& e) {
            full_error(e, p);
        }
        value = join(cwd, value, native_platform());
    }
    bool trailing = value.ends_with(u'/');
    std::vector<std::u16string> parts;
    std::size_t start = 0;
    while (start < value.size()) {
        auto end = value.find(u'/', start);
        if (end == value.npos)
            end = value.size();
        auto part = value.substr(start, end - start);
        if (part == u"..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!part.empty() && part != u".")
            parts.push_back(std::move(part));
        start = end + 1;
    }
    std::u16string result = u"/";
    for (auto& part : parts) {
        if (result.size() > 1)
            result += u'/';
        result += part;
    }
    if (trailing && result.size() > 1)
        result += u'/';
    return text(std::move(result));
#endif
}
class NativeCursor final : public DirectoryEntriesCursor {
    Text path_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    alignas(void*) std::array<std::byte, 4096> buffer_{};
    struct Info {
        ULONG next, index;
        LARGE_INTEGER creation, access, write, change, size, allocation;
        ULONG attributes, name_length, ea_size;
        WCHAR name[1];
    };
    Info* current_ = nullptr;
    bool first_ = true;
    using Query = LONG(NTAPI*)(HANDLE, HANDLE, void*, void*, IO_STATUS_BLOCK*, void*, ULONG, ULONG, BOOLEAN,
                               void*, BOOLEAN);
    using Convert = ULONG(NTAPI*)(LONG);
    Query query_;
    Convert convert_;
#else
    DIR* handle_ = nullptr;
#endif
  public:
    explicit NativeCursor(Text path) : path_(std::move(path)) {
#ifdef _WIN32
        static_assert(offsetof(Info, name) == 68);
        auto io = io_path(*path_);
        handle_ = CreateFileW(reinterpret_cast<const wchar_t*>(io.c_str()), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            auto code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND)
                code = ERROR_PATH_NOT_FOUND;
            os_error(code, *path_);
        }
        auto dll = GetModuleHandleW(L"ntdll.dll");
        query_ = reinterpret_cast<Query>(GetProcAddress(dll, "NtQueryDirectoryFile"));
        convert_ = reinterpret_cast<Convert>(GetProcAddress(dll, "RtlNtStatusToDosError"));
        if (!query_ || !convert_) {
            close();
            throw std::runtime_error("Native Nt directory enumeration unavailable");
        }
#else
        handle_ = ([&]() {
            decltype(::opendir(utf8(*path_).c_str())) result;
            do {
                result = ::opendir(utf8(*path_).c_str());
            } while (!result && errno == EINTR);
            return result;
        }());
        if (!handle_)
            os_error(errno, *path_);
#endif
    }
    ~NativeCursor() {
        close();
    }
    std::optional<DirectoryEntry> next() override {
#ifdef _WIN32
        if (handle_ == INVALID_HANDLE_VALUE)
            return {};
        if (!first_ && current_ && current_->next)
            current_ = reinterpret_cast<Info*>(reinterpret_cast<std::byte*>(current_) + current_->next);
        else {
            IO_STATUS_BLOCK status{};
            auto result = query_(handle_, nullptr, nullptr, nullptr, &status, buffer_.data(),
                                 static_cast<ULONG>(buffer_.size()), 2, FALSE, nullptr, FALSE);
            if (static_cast<ULONG>(result) == 0x80000006u || static_cast<ULONG>(result) == 0xc000000fu) {
                close();
                return {};
            }
            if (result)
                os_error(convert_(result), *path_);
            current_ = reinterpret_cast<Info*>(buffer_.data());
        }
        first_ = false;
        return DirectoryEntry{
            std::u16string(reinterpret_cast<char16_t*>(current_->name), current_->name_length / 2),
            (current_->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0};
#else
        if (!handle_)
            return {};
        errno = 0;
        auto entry = ::readdir(handle_);
        if (!entry) {
            auto error = errno;
            close();
            if (error)
                os_error(error, *path_);
            return {};
        }
        auto name = utf16_replacing(entry->d_name);
        bool directory = entry->d_type == DT_DIR;
        if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
            struct stat status{};
            auto full = join(*path_, name, native_platform());
            directory = ([&]() {
                            decltype(::stat(utf8(full).c_str(), &status)) result;
                            do {
                                result = ::stat(utf8(full).c_str(), &status);
                            } while (result < 0 && errno == EINTR);
                            return result;
                        }()) == 0 &&
                        S_ISDIR(status.st_mode);
        }
        return DirectoryEntry{std::move(name), directory};
#endif
    }
    void close() override {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        if (handle_)
            ::closedir(handle_);
        handle_ = nullptr;
#endif
    }
};
std::vector<Text> collect(Text path, Text pattern, bool directories,
                          std::shared_ptr<DirectoryEntriesBackend> backend) {
    auto platform = backend ? backend->platform : native_platform();
    auto normalized = normalize_directory_search(std::move(path), std::move(pattern), platform);
    if (!backend)
        backend = native_directory_entries_backend();
    if (!backend->full_path || !backend->open)
        throw std::invalid_argument("Directory enumeration requires full path and cursor providers");
    auto full = backend->full_path(normalized.directory);
    if (!full)
        throw std::logic_error("Directory full path provider returned null");
    auto root = text(trim(*full, platform));
    auto cursor = backend->open(root);
    if (!cursor)
        throw std::logic_error("Directory cursor provider returned null");
    std::vector<Text> out;
    try {
        while (auto entry = cursor->next()) {
            if (entry->is_directory && (entry->name == u"." || entry->name == u".."))
                continue;
            if (entry->is_directory != directories)
                continue;
            bool ignore_case = platform != Platform::Linux;
            bool match = backend->case_backend
                             ? file_system_name::matches_win32_expression(*normalized.expression, entry->name,
                                                                          ignore_case, *backend->case_backend)
                             : file_system_name::matches_win32_expression(*normalized.expression, entry->name,
                                                                          ignore_case, backend->casing);
            if (match)
                out.push_back(text(join(*normalized.directory, entry->name, platform)));
        }
    } catch (...) {
        cursor->close();
        throw;
    }
    cursor->close();
    return out;
}
}
std::shared_ptr<DirectoryEntriesBackend> native_directory_entries_backend() {
    auto backend = std::make_shared<DirectoryEntriesBackend>();
    backend->platform = native_platform();
    backend->full_path = native_full_path;
    backend->open = [](Text root) { return std::make_shared<NativeCursor>(std::move(root)); };
    return backend;
}
std::vector<Text> directory_get_files(Text path, Text pattern,
                                      std::shared_ptr<DirectoryEntriesBackend> backend) {
    return collect(std::move(path), std::move(pattern), false, std::move(backend));
}
std::vector<Text> directory_get_directories(Text path, Text pattern,
                                            std::shared_ptr<DirectoryEntriesBackend> backend) {
    return collect(std::move(path), std::move(pattern), true, std::move(backend));
}
std::vector<Text> directory_get_files(Text path) {
    return directory_get_files(std::move(path), file_system_name::star_literal());
}
std::vector<Text> directory_get_directories(Text path) {
    return directory_get_directories(std::move(path), file_system_name::star_literal());
}
}

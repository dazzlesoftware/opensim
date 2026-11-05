// .NET8.0.31 synchronous XmlDownloadManager file recipe. MIT .NET license.
// Native OS/path/error policy shared in provenance with nini_streams.cpp.
#include "opensim/framework/nini_xml_file_input.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/darwin_file_lock_policy.hpp"
#include "opensim/framework/file_stream_policy.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include "opensim/framework/full_path.hpp"
#include <filesystem>
#include <system_error>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#ifdef __APPLE__
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif
#endif
namespace opensim::framework {
namespace {
[[noreturn]] void failure(std::string type, std::uint32_t hr, std::u16string message,
                          std::optional<std::u16string> p = {}) {
    throw NiniStreamError(std::move(type), std::bit_cast<std::int32_t>(hr), std::move(message), std::move(p));
}
[[noreturn]] void missing_file(const std::u16string& path) {
    NiniStreamError e("System.IO.FileNotFoundException", std::bit_cast<std::int32_t>(0x80070002u),
                      u"Could not find file '" + path + u"'.");
    e.file_name = path;
    throw e;
}
void pathcheck(const NiniWriterText& p) {
    if (!p)
        failure("System.ArgumentNullException", 0x80004003u, u"Value cannot be null. (Parameter 'path')",
                u"path");
    if (p->empty())
        failure("System.ArgumentException", 0x80070057u,
                u"The value cannot be an empty string. (Parameter 'path')", u"path");
}
std::u16string number(std::int32_t n) {
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
}
namespace {
#ifdef _WIN32
bool sep(char16_t c) {
    return c == u'\\' || c == u'/';
}
bool extended(std::u16string_view p) {
    return p.size() >= 4 && p[0] == u'\\' && (p[1] == u'\\' || p[1] == u'?') && p[2] == u'?' && p[3] == u'\\';
}
bool device(std::u16string_view p) {
    return extended(p) ||
           (p.size() >= 4 && sep(p[0]) && sep(p[1]) && (p[2] == u'.' || p[2] == u'?') && sep(p[3]));
}
std::size_t rootlength(std::u16string_view p) {
    auto n = p.size();
    std::size_t i = 0;
    bool d = device(p), unc = d && n >= 8 && sep(p[7]) && p[4] == u'U' && p[5] == u'N' && p[6] == u'C';
    if ((!d || unc) && n && sep(p[0])) {
        if (unc || (n > 1 && sep(p[1]))) {
            i = unc ? 8 : 2;
            int remaining = 2;
            for (; i < n; ++i)
                if (sep(p[i]) && --remaining <= 0)
                    break;
        } else
            i = 1;
    } else if (d) {
        for (i = 4; i < n && !sep(p[i]); ++i) {
        }
        if (i < n && i > 4 && sep(p[i]))
            ++i;
    } else if (n >= 2 && p[1] == u':' && ((p[0] >= u'A' && p[0] <= u'Z') || (p[0] >= u'a' && p[0] <= u'z'))) {
        i = 2;
        if (n > 2 && sep(p[2]))
            ++i;
    }
    return i;
}
struct MediaPrompt {
    DWORD old_mode = 0;
    BOOL changed = SetThreadErrorMode(SEM_FAILCRITICALERRORS, &old_mode);
    ~MediaPrompt() {
        if (changed) {
            DWORD ignored = 0;
            SetThreadErrorMode(old_mode, &ignored);
        }
    }
};
std::u16string system_message(DWORD code) {
    wchar_t* b = nullptr;
    auto n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, code, 0, reinterpret_cast<wchar_t*>(&b), 0, nullptr);
    std::u16string result;
    if (n) {
        result.assign(reinterpret_cast<char16_t*>(b), n);
        LocalFree(b);
        while (!result.empty() && (result.back() == u'\r' || result.back() == u'\n' || result.back() == u' '))
            result.pop_back();
    } else
        result = u"Unknown error (0x" + number(static_cast<std::int32_t>(code)) + u")";
    return result;
}
[[noreturn]] void io_error(DWORD code, const std::u16string& path) {
    auto hr = (code & 0xffff0000u) ? code : 0x80070000u | code;
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
        missing_file(path);
    case ERROR_PATH_NOT_FOUND:
        failure("System.IO.DirectoryNotFoundException", 0x80070003u,
                u"Could not find a part of the path '" + path + u"'.");
    case ERROR_ACCESS_DENIED:
        failure("System.UnauthorizedAccessException", 0x80070005u,
                u"Access to the path '" + path + u"' is denied.");
    case ERROR_SHARING_VIOLATION:
        failure("System.IO.IOException", hr,
                u"The process cannot access the file '" + path +
                    u"' because it is being used by another process.");
    case ERROR_FILENAME_EXCED_RANGE:
        failure("System.IO.PathTooLongException", 0x800700ceu,
                u"The path '" + path + u"' is too long, or a component of the specified path is too long.");
    case ERROR_OPERATION_ABORTED:
        failure("System.OperationCanceledException", 0x8013153bu, u"The operation was canceled.");
    default:
        failure("System.IO.IOException", hr,
                system_message(code) + (path.empty() ? u"" : u" : '" + path + u"'"));
    }
}
std::u16string full_file_path(const NiniWriterText& p) {
    pathcheck(p);
    if (std::all_of(p->begin(), p->end(), [](auto c) { return c == u' '; }))
        failure("System.ArgumentException", 0x80070057u, u"The path is empty. (Parameter 'path')", u"path");
    if (p->find(u'\0') != p->npos)
        failure("System.ArgumentException", 0x80070057u, u"Null character in path. (Parameter 'path')",
                u"path");
    if (extended(*p))
        return *p;
    try {
        auto backend = native_full_path_backend();
        auto full = backend->normalize(*p);
        if (full.find(u'~') != full.npos)
            full = backend->expand_short(full);
        return full;
    } catch (const FullPathError& e) {
        if (e.win32_code)
            io_error(*e.win32_code, *p);
        throw;
    }
}
std::u16string file_io_path(const std::u16string& p) {
    if (device(p) || p.size() < 260 && (!p.ends_with(u'.') && !p.ends_with(u' ')))
        return p;
    if (p.starts_with(u"\\\\"))
        return u"\\\\?\\UNC\\" + p.substr(2);
    return u"\\\\?\\" + p;
}
#else
[[noreturn]] void io_error(int code, const std::u16string& path) {
    auto p = utf8(path);
    switch (code) {
    case ENOENT: {
        auto trimmed = std::filesystem::u8path(p);
        if (trimmed.filename().empty())
            trimmed = trimmed.parent_path();
        struct stat st{};
        auto parent = trimmed.parent_path();
        if (([&]() {
                decltype(::stat(parent.c_str(), &st)) result;
                do {
                    result = ::stat(parent.c_str(), &st);
                } while (result < 0 && errno == EINTR);
                return result;
            }()) == 0 &&
            S_ISDIR(st.st_mode))
            missing_file(path);
        [[fallthrough]];
    }
    case ENOTDIR:
        failure("System.IO.DirectoryNotFoundException", 0x80070003u,
                u"Could not find a part of the path '" + path + u"'.");
    case EACCES:
    case EBADF:
    case EPERM:
    case EISDIR:
        failure("System.UnauthorizedAccessException", 0x80070005u,
                u"Access to the path '" + path + u"' is denied.");
    case ENAMETOOLONG:
        failure("System.IO.PathTooLongException", 0x800700ceu,
                u"The path '" + path + u"' is too long, or a component of the specified path is too long.");
    case EWOULDBLOCK:
        failure("System.IO.IOException", static_cast<std::uint32_t>(code),
                u"The process cannot access the file '" + path +
                    u"' because it is being used by another process.");
    case ECANCELED:
        failure("System.OperationCanceledException", 0x8013153bu, u"The operation was canceled.");
    case EFBIG:
        failure("System.ArgumentOutOfRangeException", 0x80131502u,
                u"Specified file length was too large for the file system. (Parameter 'value')", u"value");
    case EEXIST:
        failure("System.IO.IOException", static_cast<std::uint32_t>(code),
                u"The file '" + path + u"' already exists.");
    default:
        failure("System.IO.IOException", static_cast<std::uint32_t>(code),
                utf16_replacing(std::generic_category().message(code)) +
                    (path.empty() ? u"" : u" : '" + path + u"'"));
    }
}
std::u16string full_file_path(const NiniWriterText& p) {
    pathcheck(p);
    if (p->find(u'\0') != p->npos)
        failure("System.ArgumentException", 0x80070057u, u"Null character in path. (Parameter 'path')",
                u"path");
    std::u16string path = *p;
    if (path.front() != u'/') {
        std::u16string cwd;
        try {
            cwd = utf16_replacing(get_full_path(std::string(".")));
        } catch (const FullPathError& e) {
            switch (e.kind) {
            case FullPathFailure::MissingFile:
                failure("System.IO.FileNotFoundException", 0x80070002u, utf16_replacing(e.what()));
            case FullPathFailure::MissingDirectory:
                failure("System.IO.DirectoryNotFoundException", 0x80070003u, utf16_replacing(e.what()));
            case FullPathFailure::Unauthorized:
                failure("System.UnauthorizedAccessException", 0x80070005u, utf16_replacing(e.what()));
            case FullPathFailure::PathTooLong:
                failure("System.IO.PathTooLongException", 0x800700ceu, utf16_replacing(e.what()));
            case FullPathFailure::OperationCanceled:
                failure("System.OperationCanceledException", 0x8013153bu, utf16_replacing(e.what()));
            case FullPathFailure::ArgumentOutOfRange:
                failure("System.ArgumentOutOfRangeException", 0x80131502u, utf16_replacing(e.what()),
                        e.parameter ? std::optional(utf16_replacing(*e.parameter)) : std::nullopt);
            default:
                failure("System.IO.IOException", 0x80131620u, utf16_replacing(e.what()));
            }
        }
        path = cwd + (cwd.ends_with(u'/') ? u"" : u"/") + path;
    }
    bool trailing = path.ends_with(u'/');
    std::vector<std::u16string> parts;
    std::size_t start = 0;
    while (start < path.size()) {
        auto end = path.find(u'/', start);
        if (end == path.npos)
            end = path.size();
        auto part = path.substr(start, end - start);
        if (part == u"..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!part.empty() && part != u".")
            parts.push_back(std::move(part));
        start = end + 1;
    }
    std::u16string full = u"/";
    for (auto& part : parts) {
        if (full.size() > 1)
            full += u'/';
        full += part;
    }
    if (trailing && full.size() > 1)
        full += u'/';
    return full;
}
#endif
class NativeFile final : public NiniByteStream {
    std::u16string path_;
    std::array<std::uint8_t, 0> buffer_{}; /* bufferSize1 selects the source unbuffered OS strategy */
    std::size_t read_pos_ = 0, read_len_ = 0, write_pos_ = 0;
    std::int64_t os_pos_ = 0;
    bool closed_ = true, seek_ = false, readable_ = false, writable_ = false;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
    bool locked_ = false;
#endif
    void check() {
        if (closed_)
            failure("System.ObjectDisposedException", 0x80131622u, u"Cannot access a closed file.");
    }
    std::size_t raw_read(std::span<std::uint8_t> b) {
#ifdef _WIN32
        DWORD n = 0;
        OVERLAPPED o{};
        if (seek_) {
            o.Offset = static_cast<DWORD>(os_pos_);
            o.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(os_pos_) >> 32);
        }
        if (!ReadFile(handle_, b.data(), static_cast<DWORD>(b.size()), &n, &o)) {
            auto code = GetLastError();
            if (code == ERROR_HANDLE_EOF) {
                os_pos_ += n;
                return n;
            }
            if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA)
                return 0;
            io_error(code, path_);
        }
        os_pos_ += n;
        return n;
#else
        ssize_t n;
        do {
            n = seek_ ? ::pread(handle_, b.data(), b.size(), static_cast<off_t>(os_pos_))
                      : ::read(handle_, b.data(), b.size());
        } while (n < 0 && errno == EINTR);
        if (n < 0)
            io_error(errno, path_);
        os_pos_ += n;
        return static_cast<std::size_t>(n);
#endif
    }
    void raw_write(std::span<const std::uint8_t> b) {
#ifdef _WIN32
        if (b.empty())
            return;
        DWORD n = 0;
        OVERLAPPED o{};
        if (seek_) {
            o.Offset = static_cast<DWORD>(os_pos_);
            o.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(os_pos_) >> 32);
        }
        if (!WriteFile(handle_, b.data(), static_cast<DWORD>(b.size()), &n, &o))
            io_error(GetLastError(), path_);
        os_pos_ += static_cast<std::int64_t>(b.size());
#else
        std::size_t written = 0;
        while (written < b.size()) {
            ssize_t n;
            do {
                n = seek_ ? ::pwrite(handle_, b.data() + written, b.size() - written,
                                     static_cast<off_t>(os_pos_ + written))
                          : ::write(handle_, b.data() + written, b.size() - written);
            } while (n < 0 && errno == EINTR);
            if (n < 0)
                io_error(errno, path_);
            if (!n)
                io_error(EIO, path_);
            written += static_cast<std::size_t>(n);
        }
        os_pos_ += static_cast<std::int64_t>(b.size());
#endif
    }

    void flush_write() {
        if (write_pos_) {
            raw_write(std::span(buffer_).first(write_pos_));
            write_pos_ = 0;
        }
    }
    void flush_read() {
        os_pos_ -= static_cast<std::int64_t>(read_len_ - read_pos_);
        read_pos_ = read_len_ = 0;
    }
    void release() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        if (handle_ >= 0) {
            if (locked_)
                ::flock(handle_, LOCK_UN);
            ::close(handle_);
        }
        handle_ = -1;
#endif
        closed_ = true;
    }

  public:
    NativeFile(const NiniWriterText& path, std::int32_t mode, std::int32_t access, std::int32_t share,
               std::optional<bool> override_locking) {
#ifdef _WIN32
        (void)override_locking;
#endif
        path_ = full_file_path(path);
        readable_ = access == 1;
        writable_ = access == 2;
#ifdef _WIN32
        MediaPrompt prompt;
        auto io = file_io_path(path_);
        handle_ = CreateFileW(reinterpret_cast<const wchar_t*>(io.c_str()),
                              readable_ ? GENERIC_READ : GENERIC_WRITE, static_cast<DWORD>(share), nullptr,
                              mode == 2 ? CREATE_ALWAYS : OPEN_EXISTING,
                              SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            auto code = GetLastError();
            if (code == ERROR_PATH_NOT_FOUND && path_.size() == rootlength(path_))
                code = ERROR_ACCESS_DENIED;
            io_error(code, path_);
        }
        closed_ = false;
        seek_ = GetFileType(handle_) == FILE_TYPE_DISK;
#else
        bool disable_locking = mode == 2 ? file_stream_disable_locking(override_locking) : false;
        auto native = utf8(path_);
        int flags = (readable_ ? O_RDONLY : O_WRONLY) | O_CLOEXEC;
        if (mode == 2) {
            flags |= O_CREAT;
            if (disable_locking)
                flags |= O_TRUNC;
        }
        do {
            handle_ = ::open(native.c_str(), flags, 0666);
        } while (handle_ < 0 && errno == EINTR);
        if (handle_ < 0)
            io_error(errno, path_);
        closed_ = false;
        try {
            struct stat st{};
            if (([&]() {
                    decltype(::fstat(handle_, &st)) result;
                    do {
                        result = ::fstat(handle_, &st);
                    } while (result < 0 && errno == EINTR);
                    return result;
                }()) < 0)
                io_error(errno, path_);
            if (S_ISDIR(st.st_mode))
                io_error(EACCES, path_);
            seek_ = S_ISREG(st.st_mode) || ([&]() {
                                               decltype(::lseek(handle_, 0, SEEK_CUR)) result;
                                               do {
                                                   result = ::lseek(handle_, 0, SEEK_CUR);
                                               } while (result < 0 && errno == EINTR);
                                               return result;
                                           }()) >= 0;
            if (mode != 2)
                disable_locking = file_stream_disable_locking(override_locking);
            bool lock = !disable_locking;
            if (lock && writable_ && share) {
                struct statfs fs{};
                int result;
                do {
                    result = ::fstatfs(handle_, &fs);
                } while (result < 0 && errno == EINTR);
                if (result < 0)
                    lock = false;
#ifdef __APPLE__
                else if (!darwin_shared_write_can_lock(fs.f_fstypename))
                    lock = false;
#else
                else if (static_cast<std::uint64_t>(fs.f_type) == 0x6969 ||
                         static_cast<std::uint64_t>(fs.f_type) == 0x517b ||
                         static_cast<std::uint64_t>(fs.f_type) == 0xfe534d42 ||
                         static_cast<std::uint64_t>(fs.f_type) == 0xff534d42)
                    lock = false;
#endif
            }
            if (lock) {
                int result;
                do {
                    result = ::flock(handle_, (share ? LOCK_SH : LOCK_EX) | LOCK_NB);
                } while (result < 0 && errno == EINTR);
                if (result == 0)
                    locked_ = true;
                else if (errno == EWOULDBLOCK)
                    io_error(errno, path_);
            }
            if (mode == 2 && !disable_locking && ([&]() {
                                                     decltype(::ftruncate(handle_, 0)) result;
                                                     do {
                                                         result = ::ftruncate(handle_, 0);
                                                     } while (result < 0 && errno == EINTR);
                                                     return result;
                                                 }()) < 0 &&
                errno != EBADF && errno != EINVAL)
                io_error(errno, path_);
        } catch (...) {
            release();
            throw;
        }
#endif
    }
    std::optional<std::int64_t> xml_length() {
        if (!can_seek())
            return {};
#ifdef _WIN32
        FILE_STANDARD_INFO info{};
        if (!GetFileInformationByHandleEx(handle_, FileStandardInfo, &info, sizeof(info)))
            io_error(GetLastError(), path_);
        return info.EndOfFile.QuadPart;
#else
        struct stat info{};
        int result;
        do {
            result = ::fstat(handle_, &info);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            io_error(errno, path_);
        return info.st_size;
#endif
    }
    std::int32_t xml_read(std::span<std::uint8_t> bytes) {
        check();
        return static_cast<std::int32_t>(raw_read(bytes));
    }

    ~NativeFile() {
        if (!closed_)
            try {
                close();
            } catch (...) {
                release();
            }
    }
    bool can_read() override {
        return !closed_ && readable_;
    }
    bool can_write() override {
        return !closed_ && writable_;
    }
    bool can_seek() override {
        return !closed_ && seek_;
    }
    std::int64_t position() override {
        check();
        if (!seek_)
            failure("System.NotSupportedException", 0x80131515u, u"Stream does not support seeking.");
        return os_pos_ - static_cast<std::int64_t>(read_len_ - read_pos_) +
               static_cast<std::int64_t>(write_pos_);
    }
    std::size_t read(std::span<std::uint8_t> buffer, std::size_t offset, std::size_t count) override {
        check();
        if (!readable_)
            failure("System.NotSupportedException", 0x80131515u, u"Stream does not support reading.");
        auto dest = buffer.subspan(offset, count);
        bool blocked = false;
        auto available = read_len_ - read_pos_;
        if (!available) {
            flush_write();
            if (!seek_ || count >= buffer_.size()) {
                read_pos_ = read_len_ = 0;
                return raw_read(dest);
            }
            available = raw_read(buffer_);
            if (!available)
                return 0;
            blocked = available < buffer_.size();
            read_pos_ = 0;
            read_len_ = available;
        }
        auto n = std::min(available, count);
        std::copy_n(buffer_.begin() + read_pos_, n, dest.begin());
        read_pos_ += n;
        if (seek_ && n < count && !blocked) {
            n += raw_read(dest.subspan(n));
            read_pos_ = read_len_ = 0;
        }
        return n;
    }
    void write(std::span<const std::uint8_t> b) override {
        if (closed_)
            failure("System.ObjectDisposedException", 0x80131622u, u"Cannot access a closed Stream.");
        if (!writable_)
            failure("System.NotSupportedException", 0x80131515u, u"Stream does not support writing.");
        if (!write_pos_)
            flush_read();
        if (write_pos_) {
            auto n = std::min(buffer_.size() - write_pos_, b.size());
            std::copy_n(b.begin(), n, buffer_.begin() + write_pos_);
            write_pos_ += n;
            b = b.subspan(n);
            if (b.empty())
                return;
            flush_write();
        }
        if (b.size() >= buffer_.size())
            raw_write(b);
        else if (!b.empty()) {
            std::copy(b.begin(), b.end(), buffer_.begin());
            write_pos_ = b.size();
        }
    }
    void flush() override {
        check();
        if (write_pos_)
            flush_write();
        else if (read_len_ && seek_)
            flush_read();
    }
    void close() override {
        if (closed_)
            return;
        try {
            flush();
        } catch (...) {
            write_pos_ = 0;
            release();
            throw;
        }
        write_pos_ = 0;
        release();
    }
};
}
namespace {
[[noreturn]] void xml_stream_error(const NiniStreamError& e) {
    NiniXmlProviderError x(e.source_type, static_cast<std::uint32_t>(e.hresult), e.message, e.parameter);
    x.file_name = e.file_name;
    x.object_name = e.object_name;
    throw x;
}
class XmlFileInput final : public NativeNiniXmlByteInput {
    std::shared_ptr<NativeFile> file_;

  public:
    explicit XmlFileInput(NiniWriterText path, std::optional<bool> locking) {
        try {
            file_ = std::make_shared<NativeFile>(path, 3, 1, 1, locking);
        } catch (const NiniStreamError& e) {
            xml_stream_error(e);
        }
    }
    std::int32_t read(std::span<std::uint8_t> b) override {
        try {
            return file_->xml_read(b);
        } catch (const NiniStreamError& e) {
            xml_stream_error(e);
        }
    }
    std::optional<std::int64_t> length_hint() const override {
        try {
            return file_->xml_length();
        } catch (const NiniStreamError& e) {
            xml_stream_error(e);
        }
    }
    void close() override {
        try {
            file_->close();
        } catch (const NiniStreamError& e) {
            xml_stream_error(e);
        }
    }
};
}
std::shared_ptr<NativeNiniXmlByteInput> native_nini_xml_file_input(NiniWriterText path,
                                                                   std::optional<bool> locking) {
    return std::make_shared<XmlFileInput>(std::move(path), locking);
}
NiniWriterText native_nini_xml_full_path(NiniWriterText path) {
#ifdef _WIN32
    if (path && path->empty())
        throw NiniXmlProviderError("System.ArgumentException", 0x80070057,
                                   u"The path is empty. (Parameter 'path')", u"path");
#endif
    try {
        return nini_writer_text(full_file_path(path));
    } catch (const NiniStreamError& e) {
        xml_stream_error(e);
    }
}
}

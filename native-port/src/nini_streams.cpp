// Derived from .NET8 StreamReader/StreamWriter synchronous consumed methods.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// ../third_party/dotnet_collation/LICENSE.
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
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
NiniStreamError::NiniStreamError(std::string t, std::int32_t h, std::u16string m,
                                 std::optional<std::u16string> p)
    : runtime_error(utf8(m)), source_type(std::move(t)), hresult(h), message(std::move(m)),
      parameter(std::move(p)) {
    if (source_type == "System.ObjectDisposedException") {
        object_name = u"";
        if (message.find(u"Object name: 'StreamReader'.") != message.npos)
            object_name = u"StreamReader";
        else if (message.find(u"Object name: 'StreamWriter'.") != message.npos)
            object_name = u"StreamWriter";
    }
}
namespace {
[[noreturn]] void failure(std::string type, std::uint32_t hr, std::u16string message,
                          std::optional<std::u16string> p = {}) {
    throw NiniStreamError(std::move(type), std::bit_cast<std::int32_t>(hr), std::move(message), std::move(p));
}
[[noreturn]] void missing_file(const std::u16string& path) {
    NiniStreamError error("System.IO.FileNotFoundException", std::bit_cast<std::int32_t>(0x80070002u),
                          u"Could not find file '" + path + u"'.");
    error.file_name = path;
    throw error;
}
void streamcheck(const std::shared_ptr<NiniByteStream>& stream) {
    if (!stream)
        failure("System.ArgumentNullException", 0x80004003u, u"Value cannot be null. (Parameter 'stream')",
                u"stream");
}
std::u16string newline() {
#ifdef _WIN32
    return u"\r\n";
#else
    return u"\n";
#endif
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
std::size_t utf8_stable(std::span<const std::uint8_t> bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        auto first = bytes[i];
        std::size_t need = first >= 0xc2 && first <= 0xdf   ? 2
                           : first >= 0xe0 && first <= 0xef ? 3
                           : first >= 0xf0 && first <= 0xf4 ? 4
                                                            : 1;
        if (need == 1) {
            ++i;
            continue;
        }
        std::size_t n = 1;
        for (; n < need && i + n < bytes.size(); ++n) {
            auto c = bytes[i + n];
            if (c < 0x80 || c > 0xbf ||
                (n == 1 && ((first == 0xe0 && c < 0xa0) || (first == 0xed && c >= 0xa0) ||
                            (first == 0xf0 && c < 0x90) || (first == 0xf4 && c >= 0x90))))
                break;
        }
        if (n < need) {
            if (i + n == bytes.size())
                return i;
            i += n;
        } else
            i += need;
    }
    return i;
}
std::vector<std::uint8_t> preamble(StreamReaderEncoding encoding) {
    switch (encoding) {
    case StreamReaderEncoding::Utf16LE:
        return {0xff, 0xfe};
    case StreamReaderEncoding::Utf16BE:
        return {0xfe, 0xff};
    case StreamReaderEncoding::Utf32LE:
        return {0xff, 0xfe, 0, 0};
    case StreamReaderEncoding::Utf32BE:
        return {0, 0, 0xfe, 0xff};
    default:
        return {0xef, 0xbb, 0xbf};
    }
}
struct Decoder {
    StreamReaderEncoding encoding = StreamReaderEncoding::Utf8;
    std::vector<std::uint8_t> pending;
    std::u16string decode(std::span<const std::uint8_t> input, bool flush) {
        std::vector<std::uint8_t> bytes = pending;
        bytes.insert(bytes.end(), input.begin(), input.end());
        std::size_t count = bytes.size();
        if (!flush) {
            if (encoding == StreamReaderEncoding::Utf8)
                count = utf8_stable(bytes);
            else if (encoding == StreamReaderEncoding::Utf16LE || encoding == StreamReaderEncoding::Utf16BE) {
                count -= count % 2;
                if (count >= 2) {
                    auto a = bytes[count - 2], b = bytes[count - 1];
                    auto last = encoding == StreamReaderEncoding::Utf16LE
                                    ? std::uint32_t(a) | (std::uint32_t(b) << 8)
                                    : (std::uint32_t(a) << 8) | b;
                    if (last >= 0xd800 && last <= 0xdbff)
                        count -= 2;
                }
            } else
                count -= count % 4;
        }
        auto bom = preamble(encoding);
        bool sentinel =
            encoding == StreamReaderEncoding::Utf16LE || encoding == StreamReaderEncoding::Utf16BE;
        if (sentinel) {
            if (encoding == StreamReaderEncoding::Utf16LE) {
                bom.push_back(0x41);
                bom.push_back(0);
            } else {
                bom.push_back(0);
                bom.push_back(0x41);
            }
        }
        bom.insert(bom.end(), bytes.begin(), bytes.begin() + count);
        auto out = decode_stream_reader_text(bom).text;
        if (sentinel)
            out.erase(out.begin());
        pending.assign(bytes.begin() + count, bytes.end());
        return out;
    }
};
}
struct NiniByteTextReader::State {
    std::shared_ptr<NiniByteStream> stream;
    std::array<std::uint8_t, 1024> bytes{};
    std::size_t byte_len = 0, byte_pos = 0, char_pos = 0;
    std::u16string chars;
    bool check_preamble = true, detect_encoding = true, disposed = false, blocked = false;
    Decoder decoder;
    explicit State(std::shared_ptr<NiniByteStream> stream) : stream(std::move(stream)) {
        streamcheck(this->stream);
        if (!this->stream->can_read())
            failure("System.ArgumentException", 0x80070057u, u"Stream was not readable.");
    }
    void check() {
        if (disposed)
            failure("System.ObjectDisposedException", 0x80131622u,
                    u"Cannot read from a closed TextReader." + newline() + u"Object name: 'StreamReader'.");
    }
    void compress(std::size_t n) {
        byte_len -= n;
        std::move(bytes.begin() + n, bytes.begin() + n + byte_len, bytes.begin());
    }
    bool is_preamble() {
        if (!check_preamble)
            return false;
        constexpr std::array<std::uint8_t, 3> bom{0xef, 0xbb, 0xbf};
        auto n = std::min(byte_len, bom.size());
        for (auto i = byte_pos; i < n; ++i)
            if (bytes[i] != bom[i]) {
                byte_pos = 0;
                check_preamble = false;
                return false;
            }
        byte_pos = n;
        if (byte_pos == bom.size()) {
            compress(bom.size());
            byte_pos = 0;
            check_preamble = false;
            detect_encoding = false;
        }
        return check_preamble;
    }
    void detect() {
        detect_encoding = false;
        bool changed = false;
        auto n = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8);
        if (n == 65534) {
            decoder.encoding = StreamReaderEncoding::Utf16BE;
            compress(2);
            changed = true;
        } else if (n == 65279) {
            if (byte_len < 4 || bytes[2] != 0 || bytes[3] != 0) {
                decoder.encoding = StreamReaderEncoding::Utf16LE;
                compress(2);
            } else {
                decoder.encoding = StreamReaderEncoding::Utf32LE;
                compress(4);
            }
            changed = true;
        } else if (byte_len >= 3 && n == 48111 && bytes[2] == 191) {
            decoder.encoding = StreamReaderEncoding::Utf8;
            compress(3);
            changed = true;
        } else if (byte_len >= 4 && n == 0 && bytes[2] == 254 && bytes[3] == 255) {
            decoder.encoding = StreamReaderEncoding::Utf32BE;
            compress(4);
            changed = true;
        } else if (byte_len == 2)
            detect_encoding = true;
        if (changed)
            decoder.pending.clear();
    }
    std::size_t fill() {
        chars.clear();
        char_pos = 0;
        if (!check_preamble)
            byte_len = 0;
        bool eof = false;
        do {
            if (check_preamble) {
                auto n = stream->read(bytes, byte_pos, bytes.size() - byte_pos);
                if (n == 0) {
                    eof = true;
                    break;
                }
                byte_len += n;
                blocked = byte_len < bytes.size();
            } else {
                byte_len = stream->read(bytes, 0, bytes.size());
                blocked = byte_len < bytes.size();
                if (byte_len == 0) {
                    eof = true;
                    break;
                }
            }
            if (!is_preamble()) {
                if (detect_encoding && byte_len >= 2)
                    detect();
                chars = decoder.decode(std::span(bytes).first(byte_len), false);
            }
        } while (chars.empty());
        if (eof) {
            chars = decoder.decode(std::span(bytes).first(byte_len), true);
            byte_pos = 0;
            byte_len = 0;
        }
        return chars.size();
    }
};
NiniByteTextReader::NiniByteTextReader(std::shared_ptr<NiniByteStream> s)
    : state_(std::make_shared<State>(std::move(s))) {
}
int NiniByteTextReader::peek() {
    auto s = state_;
    s->check();
    if (s->char_pos == s->chars.size() && !s->fill())
        return -1;
    return s->chars[s->char_pos];
}
int NiniByteTextReader::read() {
    auto s = state_;
    s->check();
    if (s->char_pos == s->chars.size() && !s->fill())
        return -1;
    return s->chars[s->char_pos++];
}
std::int32_t NiniByteTextReader::read_chars(std::span<char16_t> destination) {
    auto s = state_;
    s->check();
    std::size_t total = 0;
    while (total < destination.size()) {
        if (s->char_pos == s->chars.size()) {
            if (!s->fill())
                break; // ReadBuffer(userBuffer) clears a short-read flag when buffered chars
            // already fill the remaining request, even though more remain buffered.
            s->blocked = s->blocked && (s->chars.size() < destination.size() - total);
        }
        auto count = std::min(destination.size() - total, s->chars.size() - s->char_pos);
        std::copy_n(s->chars.begin() + static_cast<std::ptrdiff_t>(s->char_pos), count,
                    destination.begin() + static_cast<std::ptrdiff_t>(total));
        s->char_pos += count;
        total += count;
        if (s->blocked)
            break;
    }
    return static_cast<std::int32_t>(total);
}
namespace {
class XmlStreamReaderInput final : public NativeNiniXmlTextInput {
    std::shared_ptr<NiniByteTextReader> reader_;

  public:
    explicit XmlStreamReaderInput(std::shared_ptr<NiniByteStream> stream)
        : reader_(std::make_shared<NiniByteTextReader>(std::move(stream))) {
    }
    std::int32_t read(std::span<char16_t> destination) override {
        return reader_->read_chars(destination);
    }
    void close() override {
        reader_->close();
    }
};
}
std::shared_ptr<NativeNiniXmlTextInput>
native_xml_stream_reader_input(std::shared_ptr<NiniByteStream> stream) {
    return std::make_shared<XmlStreamReaderInput>(std::move(stream));
}
void NiniByteTextReader::close() {
    auto s = state_;
    if (s->disposed)
        return;
    s->disposed = true;
    try {
        s->stream->close();
    } catch (...) {
        s->chars.clear();
        s->char_pos = 0;
        throw;
    }
    s->chars.clear();
    s->char_pos = 0;
}
StreamReaderEncoding NiniByteTextReader::current_encoding() const {
    return state_->decoder.encoding;
}
std::shared_ptr<NiniByteStream> NiniByteTextReader::base_stream() const {
    return state_->stream;
}
NiniByteTextWriter::NiniByteTextWriter(std::shared_ptr<NiniByteStream> stream)
    : NativeXmlStreamWriter(std::move(stream)) {
}
NiniWriterFactory nini_byte_writer_factory(NiniFileStreamProvider file) {
    NiniWriterFactory factory;
    factory.open_file = [file](const NiniWriterText& path, std::int32_t mode, std::int32_t access,
                               std::int32_t share) -> std::shared_ptr<NiniWriterStream> {
        if (!file)
            throw std::invalid_argument("Missing native FileStream provider");
        return file(path, mode, access, share, 4096, 0);
    };
    factory.stream_writer =
        [](std::shared_ptr<NiniWriterStream> stream) -> std::shared_ptr<NiniStreamWriter> {
        auto bytes = std::dynamic_pointer_cast<NiniByteStream>(stream);
        if (stream && !bytes)
            throw std::invalid_argument("Nini StreamWriter requires byte-stream operations");
        return std::make_shared<NiniByteTextWriter>(bytes);
    };
    return factory;
}
NiniDocumentFactory nini_byte_document_factory(NiniFileStreamProvider file) {
    NiniDocumentFactory factory;
    factory.file_reader = [file](const NiniWriterText& path) -> std::shared_ptr<NiniTextReader> {
        pathcheck(path);
        if (!file)
            throw std::invalid_argument("Missing native FileStream provider");
        return std::make_shared<NiniByteTextReader>(file(path, 3, 1, 1, 4096, 0));
    };
    factory.stream_reader = [](std::shared_ptr<NiniWriterStream> stream) -> std::shared_ptr<NiniTextReader> {
        auto bytes = std::dynamic_pointer_cast<NiniByteStream>(stream);
        if (stream && !bytes)
            throw std::invalid_argument("Nini StreamReader requires byte-stream operations");
        return std::make_shared<NiniByteTextReader>(bytes);
    };
    factory.file_writer = [file](const NiniWriterText& path) -> std::shared_ptr<NiniTextWriter> {
        pathcheck(path);
        if (!file)
            throw std::invalid_argument("Missing native FileStream provider");
        return std::make_shared<NiniByteTextWriter>(file(path, 2, 2, 1, 4096, 0));
    };
    factory.stream_writer = [](std::shared_ptr<NiniWriterStream> stream) -> std::shared_ptr<NiniTextWriter> {
        auto bytes = std::dynamic_pointer_cast<NiniByteStream>(stream);
        if (stream && !bytes)
            throw std::invalid_argument("Nini StreamWriter requires byte-stream operations");
        return std::make_shared<NiniByteTextWriter>(bytes);
    };
    return factory;
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
    std::array<std::uint8_t, 4096> buffer_{};
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
NiniFileStreamProvider native_nini_file_stream_provider(std::optional<bool> override_locking) {
    return [override_locking](const NiniWriterText& path, std::int32_t mode, std::int32_t access,
                              std::int32_t share, std::int32_t buffer,
                              std::int32_t options) -> std::shared_ptr<NiniByteStream> {
        if (!((mode == 2 && access == 2 && (share == 0 || share == 1)) ||
              (mode == 3 && access == 1 && (share == 1 || share == 3))) ||
            buffer != 4096 || options != 0)
            throw std::invalid_argument(
                "Native Nini file provider requires consumed Open/Read/ShareRead or Create/Write/ShareNone-or-Read recipes");
        return std::make_shared<NativeFile>(path, mode, access, share, override_locking);
    };
}
NiniWriterFactory native_nini_writer_factory() {
    return nini_byte_writer_factory(native_nini_file_stream_provider());
}
NiniDocumentFactory native_nini_document_factory() {
    return nini_byte_document_factory(native_nini_file_stream_provider());
}

}

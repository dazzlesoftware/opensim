#include "opensim/framework/config_http_headers_body.hpp"
#include "opensim/framework/text.hpp"
#include <limits>
#include <algorithm>
namespace opensim::framework {
namespace {
std::u16string num(std::uint64_t v) {
    return utf16(std::to_string(v));
}
std::u16string ascii(std::span<const std::uint8_t> v) {
    std::u16string s;
    for (auto b : v)
        s += b < 128 ? char16_t(b) : u'?';
    return s;
}
std::u16string dashhex(std::span<const std::uint8_t> v) {
    std::u16string s;
    constexpr char16_t hex[] = u"0123456789ABCDEF";
    for (auto b : v) {
        if (!s.empty())
            s += u'-';
        s += hex[b >> 4];
        s += hex[b & 15];
    }
    return s;
}
[[noreturn]] void io(std::u16string message, int code = 9) {
    throw ConfigHttpHeaderError(
        "System.Net.Http.HttpIOException",
        std::move(message) + (code == 10 ? u" (ResponseEnded)" : u" (InvalidResponse)"), 0x80131620, code);
}
bool equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto c = a[i], d = b[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (d >= 'A' && d <= 'Z')
            d += 32;
        if (c != d)
            return false;
    }
    return true;
}
}
struct ConfigHttpXmlBodyReader::State {
    enum class Mode { Empty, Close, Length, Chunked };
    enum class Chunk { Header, Data, Terminator, Trailers, Done };
    Mode mode = Mode::Close;
    Chunk chunk = Chunk::Header;
    ConfigHttpRawRead raw;
    std::uint64_t remaining = 0;
    std::size_t trailer_budget = 0, configured_limit = 65536;
    bool done = false, failed = false;
    std::vector<std::uint8_t> line() {
        std::vector<std::uint8_t> bytes;
        while (true) {
            std::uint8_t b;
            if (raw({&b, 1}) == 0)
                io(u"The response ended prematurely.", 10);
            bytes.push_back(b);
            if (b == 10) {
                if (bytes.size() > 16384)
                    throw ConfigHttpHeaderError("System.Net.Http.HttpRequestException",
                                                u"The HTTP/1.1 response chunk was too large.", 0x80131500, 0);
                bytes.pop_back();
                if (!bytes.empty() && bytes.back() == 13)
                    bytes.pop_back();
                return bytes;
            }
            if (bytes.size() >= 16384)
                throw ConfigHttpHeaderError("System.Net.Http.HttpRequestException",
                                            u"The HTTP/1.1 response chunk was too large.", 0x80131500, 0);
        }
    }
};
ConfigHttpXmlBodyReader::ConfigHttpXmlBodyReader(const ConfigHttpResponseHead& head, ConfigHttpRawRead raw,
                                                 std::size_t maximum)
    : state_(std::make_shared<State>()) {
    if (!raw)
        throw std::invalid_argument("HTTP XML body requires raw read provider");
    auto& s = *state_;
    s.raw = std::move(raw);
    s.configured_limit = maximum;
    s.trailer_budget = maximum > head.bytes_consumed ? maximum - head.bytes_consumed : 0;
    if (head.status == 204 || head.status == 304) {
        s.mode = State::Mode::Empty;
        return;
    }
    if (head.status == 101)
        return;
    std::vector<std::u16string> lengths, encodings;
    for (auto& h : head.headers) {
        if (equal(h.name, "Content-Length"))
            lengths.push_back(h.value);
        if (equal(h.name, "Transfer-Encoding"))
            encodings.push_back(h.value);
    }
    if (config_http_transfer_encoding_chunked(encodings))
        s.mode = State::Mode::Chunked;
    else if (auto length = config_http_content_length(lengths)) {
        s.mode = State::Mode::Length;
        s.remaining = *length;
    }
}
std::int32_t ConfigHttpXmlBodyReader::read_impl(std::span<std::uint8_t> buffer) {
    auto& s = *state_;
    if (buffer.empty() || s.done || s.mode == State::Mode::Empty)
        return 0;
    if (s.mode == State::Mode::Close) {
        auto n = s.raw(buffer);
        if (!n)
            s.done = true;
        return n;
    }
    if (s.mode == State::Mode::Length) {
        if (!s.remaining)
            return 0;
        auto n = s.raw(
            buffer.first(static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), s.remaining))));
        if (!n)
            io(u"The response ended prematurely, with at least " + num(s.remaining) +
                   u" additional bytes expected.",
               10);
        s.remaining -= n;
        return n;
    }
    while (true) {
        switch (s.chunk) {
        case State::Chunk::Header: {
            auto line = s.line();
            std::size_t i = 0;
            std::uint64_t size = 0;
            bool valid = false, overflow = false;
            for (; i < line.size(); ++i) {
                auto c = line[i];
                int v = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                               : -1;
                if (v < 0)
                    break;
                valid = true;
                if (size > (std::numeric_limits<std::uint64_t>::max() - v) / 16)
                    overflow = true;
                size = size * 16 + v;
            }
            if (!valid || overflow)
                io(u"Received chunk header length could not be parsed: '" + dashhex(line) + u"'.");
            if (i < line.size()) {
                auto start = i;
                for (; i < line.size(); ++i) {
                    if (line[i] == ';')
                        break;
                    if (line[i] != 9 && line[i] != 32)
                        io(u"Received an invalid chunk extension: '" +
                           dashhex(std::span<const std::uint8_t>(line).subspan(start)) + u"'.");
                }
            }
            s.remaining = size;
            s.chunk = size ? State::Chunk::Data : State::Chunk::Trailers;
            break;
        }
        case State::Chunk::Data: {
            auto n = s.raw(
                buffer.first(static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), s.remaining))));
            if (!n)
                io(u"The response ended prematurely.", 10);
            s.remaining -= n;
            if (!s.remaining)
                s.chunk = State::Chunk::Terminator;
            return n;
        }
        case State::Chunk::Terminator: {
            auto line = s.line();
            if (!line.empty())
                io(u"Received an invalid chunk terminator: '" + ascii(line) + u"'.");
            s.chunk = State::Chunk::Header;
            break;
        }
        case State::Chunk::Trailers: {
            constexpr std::string_view prefix = "HTTP/1.1 200 \r\n";
            std::vector<std::uint8_t> bytes(prefix.begin(), prefix.end());
            if (!s.trailer_budget)
                config_http_parse_response_head(bytes, prefix.size(), s.configured_limit);
            while (true) {
                std::uint8_t b;
                if (!s.raw({&b, 1}))
                    io(u"The response ended prematurely.", 10);
                bytes.push_back(b);
                if (b == 10 || bytes.size() >= s.trailer_budget + prefix.size() ||
                    (bytes.size() > 1 && bytes[bytes.size() - 2] == 10)) {
                    if (config_http_parse_response_head(bytes, s.trailer_budget + prefix.size(),
                                                        s.configured_limit))
                        break;
                }
            }
            s.chunk = State::Chunk::Done;
            s.done = true;
            return 0;
        }
        case State::Chunk::Done:
            return 0;
        }
    }
}
std::int32_t ConfigHttpXmlBodyReader::read(std::span<std::uint8_t> buffer) {
    try {
        return read_impl(buffer);
    } catch (...) {
        state_->failed = true;
        throw;
    }
}
bool ConfigHttpXmlBodyReader::completed_for_reuse() const noexcept {
    const auto& s = *state_;
    if (s.failed)
        return false;
    switch (s.mode) {
    case State::Mode::Empty:
        return true;
    case State::Mode::Length:
        return s.remaining == 0;
    case State::Mode::Chunked:
        return s.chunk == State::Chunk::Done;
    case State::Mode::Close:
        return false;
    }
    return false;
}
bool ConfigHttpXmlBodyReader::can_drain_for_reuse(std::size_t buffered, std::size_t maximum) const noexcept {
    const auto& s = *state_;
    if (s.failed)
        return false;
    if (s.mode == State::Mode::Chunked)
        return true;
    if (s.mode == State::Mode::Length)
        return s.remaining <= maximum || s.remaining - maximum <= buffered;
    return false;
}

}

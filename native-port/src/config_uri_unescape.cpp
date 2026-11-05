// .NET8.0.31 UriHelper.UnescapeString/PercentEncodingHelper; MIT .NET license.
#include "opensim/framework/config_uri_unescape.hpp"
#include <array>
#include <cstdint>
namespace opensim::framework {
namespace {
int hex(char16_t c) {
    return c >= u'0' && c <= u'9'   ? c - u'0'
           : c >= u'A' && c <= u'F' ? c - u'A' + 10
           : c >= u'a' && c <= u'f' ? c - u'a' + 10
                                    : -1;
}
char16_t decode(char16_t a, char16_t b) {
    auto x = hex(a), y = hex(b);
    return x < 0 || y < 0 ? char16_t(0xffff) : char16_t((x << 4) | y);
}
bool unsafe(char16_t c) {
    return c <= 31 || (c >= 127 && c <= 159) ||
           std::u16string_view(u";/?:@&=+$,#[]!'()*%\\#").find(c) != std::u16string_view::npos;
}
bool iri(std::uint32_t c, bool query) {
    if (c <= 65535)
        return (c >= 160 && c <= 55295) || (c >= 63744 && c <= 64975) || (c >= 65008 && c <= 65519) ||
               (query && c >= 57344 && c <= 63743);
    return (c & 65535) < 65534 && !(c >= 917504 && c <= 921599) && (query || c < 983040);
}
void scalar(std::u16string& out, std::uint32_t c) {
    if (c <= 65535)
        out += static_cast<char16_t>(c);
    else {
        c -= 65536;
        out += static_cast<char16_t>(0xd800 + (c >> 10));
        out += static_cast<char16_t>(0xdc00 + (c & 1023));
    }
}
void pct(std::u16string& out, std::uint8_t c) {
    out += u'%';
    out += u"0123456789ABCDEF"[c >> 4];
    out += u"0123456789ABCDEF"[c & 15];
}
// Rune.DecodeFromUtf8's exact scalar/status/bytesConsumed contract.
struct RuneResult {
    bool done = false;
    std::uint32_t value = 0;
    int used = 1;
};
RuneResult rune(const std::array<std::uint8_t, 4>& b, int n) {
    auto c = b[0];
    int need = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!need)
        return {};
    std::uint32_t cp = c & ((1u << (7 - need)) - 1);
    for (int i = 1; i < need; ++i) {
        if (i >= n)
            return {false, 0, n};
        auto v = b[i];
        if (v < 128 || v > 191 ||
            (i == 1 && ((c == 0xe0 && v < 160) || (c == 0xed && v >= 160) || (c == 0xf0 && v < 144) ||
                        (c == 0xf4 && v >= 144))))
            return {false, 0, i};
        cp = (cp << 6) | (v & 63);
    }
    return {true, cp, need};
}
std::size_t utf8(std::u16string_view input, std::u16string& out, bool query, bool parsing) {
    std::array<std::uint8_t, 4> bytes{};
    int count = 0;
    std::size_t consumed = 0, pending = 0;
    for (;;) {
        auto pos = consumed + count * 3;
        while (count < 4 && input.size() - pos > 2 && input[pos] == u'%') {
            auto a = hex(input[pos + 1]), b = hex(input[pos + 2]);
            if (a < 8 || b < 0)
                break;
            bytes[count++] = static_cast<std::uint8_t>((a << 4) | b);
            pos += 3;
        }
        if (count <= 1) {
            if (!count && !pending)
                return consumed;
            out.append(input.substr(consumed - pending, pending + count * 3));
            return consumed + count * 3;
        }
        auto r = rune(bytes, count);
        if (r.done && (!parsing || iri(r.value, query))) {
            if (pending) {
                out.append(input.substr(consumed - pending, pending));
                pending = 0;
            }
            scalar(out, r.value);
        } else
            pending += r.used * 3;
        for (int i = r.used; i < count; ++i)
            bytes[i - r.used] = bytes[i];
        count -= r.used;
        consumed += r.used * 3;
    }
}
}
std::u16string config_uri_unescape(std::u16string_view input, char16_t r1, char16_t r2, char16_t r3, int mode,
                                   bool syntax, bool query) {
    if (!(mode & 3))
        return std::u16string(input);
    std::u16string out;
    bool escape = false, parsing = syntax && (mode & 3) == 3;
    std::size_t start = 0, i = 0;
    while (i < input.size()) {
        char16_t c = 0;
        for (; i < input.size(); ++i) {
            c = input[i];
            if (c == u'%') {
                if (!(mode & 2)) {
                    escape = true;
                    break;
                }
                if (i + 2 < input.size()) {
                    c = decode(input[i + 1], input[i + 2]);
                    if (mode < 8) {
                        if (c == 0xffff) {
                            if (!(mode & 1))
                                continue;
                            escape = true;
                        } else if (c == u'%') {
                            i += 2;
                            continue;
                        } else {
                            if (c == r1 || c == r2 || c == r3) {
                                i += 2;
                                continue;
                            }
                            if (!(mode & 4) && unsafe(c)) {
                                i += 2;
                                continue;
                            }
                            if (parsing && ((c <= 159 && unsafe(c)) || (c > 159 && !iri(c, query)))) {
                                i += 2;
                                continue;
                            }
                        }
                        break;
                    }
                    if (c != 0xffff)
                        break;
                    if (mode >= 24)
                        throw ConfigUriUnescapeError();
                } else {
                    if (mode < 8) {
                        escape = true;
                        break;
                    }
                    if (mode >= 24)
                        throw ConfigUriUnescapeError();
                }
            } else if ((mode & 10) != 10 && (mode & 1)) {
                if (c == r1 || c == r2 || c == r3) {
                    escape = true;
                    break;
                }
                if (!(mode & 4) && (c <= 31 || (c >= 127 && c <= 159))) {
                    escape = true;
                    break;
                }
            }
        }
        out.append(input.substr(start, i - start));
        if (i != input.size()) {
            if (escape) {
                pct(out, static_cast<std::uint8_t>(input[i]));
                escape = false;
                ++i;
            } else if (c <= 127) {
                out += c;
                i += 3;
            } else
                i += utf8(input.substr(i), out, query, parsing);
            start = i;
        }
    }
    return out;
}
}

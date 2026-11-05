// Consumed System.Uri v8.0.31 closure; MIT .NET license:
// native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_uri.hpp"
#include <algorithm>
#include "opensim/framework/viewer_unicode_data.hpp"
#include <cstdint>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace opensim::framework {
namespace {
using V = std::u16string_view;
bool digit(char16_t c) {
    return c >= u'0' && c <= u'9';
}
bool letter(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}
bool lws(char16_t c) {
    return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n';
}
int hex(char16_t c) {
    return digit(c)                 ? c - u'0'
           : c >= u'a' && c <= u'f' ? c - u'a' + 10
           : c >= u'A' && c <= u'F' ? c - u'A' + 10
                                    : -1;
}
bool bidi(char16_t c) {
    return c == 0x200e || c == 0x200f || (c >= 0x202a && c <= 0x202e);
}
bool valid_unicode(V s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto c = s[i];
        if (c == 0xfffe || (c >= 0xdc00 && c <= 0xdfff))
            return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 1 == s.size() || s[i + 1] < 0xdc00 || s[i + 1] > 0xdfff)
                return false;
            ++i;
        }
    }
    return true;
}
bool iri(std::uint32_t c, bool query) {
    if (c <= 0xffff)
        return (c >= 160 && c <= 55295) || (c >= 63744 && c <= 64975) || (c >= 65008 && c <= 65519) ||
               (query && c >= 57344 && c <= 63743);
    return (c & 0xffff) < 65534 && !(c >= 917504 && c <= 921599) && (query || c < 983040);
}
bool iri_trigger(V s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto c = s[i];
        if (c > 127)
            return true;
        if (c == u'%' && i + 2 < s.size()) {
            int a = hex(s[i + 1]), b = hex(s[i + 2]);
            int d = a < 0 || b < 0 ? 65535 : a * 16 + b;
            if (d > 127 || letter(char16_t(d)) || digit(char16_t(d)) || d == '-' || d == '_' || d == '.' ||
                d == '~')
                return true;
            i += 2;
        }
    }
    return false;
}
// EscapeUnescapeIri changes only length for this predicate. Keep component boundaries.
std::size_t iri_length(V s, bool query) {
    std::size_t out = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t c = s[i];
        if (c == u'%' && i + 2 < s.size()) {
            int a = hex(s[i + 1]), b = hex(s[i + 2]);
            if (a < 0 || b < 0) {
                out += 3;
                i += 2;
                continue;
            }
            c = a * 16 + b;
            if (c < 128) {
                out +=
                    (c <= 31 || c == 127 || V(u";/?:@&=+$,#[]!'()*%\\").find(char16_t(c)) != V::npos) ? 3 : 1;
                i += 2;
                continue;
            }
            // Consume one valid percent-encoded UTF8 scalar; malformed bytes remain escaped.
            int count = c >= 0xc2 && c <= 0xdf   ? 2
                        : c >= 0xe0 && c <= 0xef ? 3
                        : c >= 0xf0 && c <= 0xf4 ? 4
                                                 : 0;
            std::uint32_t scalar = count ? c & ((1u << (7 - count)) - 1) : 0;
            bool ok = count > 0;
            for (int n = 1; ok && n < count; ++n) {
                auto p = i + 3 * n;
                if (p + 2 >= s.size() || s[p] != u'%') {
                    ok = false;
                    break;
                }
                int x = hex(s[p + 1]), y = hex(s[p + 2]);
                if (x < 0 || y < 0 || x * 16 + y < 0x80 || x * 16 + y > 0xbf) {
                    ok = false;
                    break;
                }
                scalar = (scalar << 6) | (x * 16 + y - 0x80);
            }
            if (ok && (scalar < (count == 2   ? 0x80u
                                 : count == 3 ? 0x800u
                                              : 0x10000u) ||
                       scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)))
                ok = false;
            if (ok) {
                out += iri(scalar, query) ? (scalar > 0xffff ? 2 : 1) : 3 * count;
                i += 3 * count - 1;
            } else {
                out += 3;
                i += 2;
            }
            continue;
        }
        if (c > 127) {
            bool pair =
                c >= 0xd800 && c <= 0xdbff && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff;
            if (pair)
                c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
            if (iri(c, query))
                out += pair ? 2 : 1;
            else {
                if (!pair && c >= 0xd800 && c <= 0xdfff)
                    c = 0xfffd;
                out += c < 0x800 ? 6 : c <= 0xffff ? 9 : 12;
            }
        } else
            ++out;
    }
    return out;
}
bool ipv4_canonical(V s, std::size_t start, std::size_t& end) {
    int dots = 0, num = 0;
    bool had = false, zero = false;
    while (start < end) {
        auto c = s[start];
        if (c == u']' || c == u'/' || c == u'%')
            break;
        if (digit(c)) {
            if (!had && c == u'0') {
                if (start + 1 < end && s[start + 1] == u'0')
                    return false;
                zero = true;
            }
            had = true;
            num = num * 10 + c - u'0';
            if (num > 255)
                return false;
        } else {
            if (c != u'.' || !had || (num > 0 && zero))
                return false;
            ++dots;
            had = false;
            num = 0;
            zero = false;
        }
        ++start;
    }
    if (dots != 3 || !had)
        return false;
    end = start;
    return true;
}
bool ipv6(V s, std::size_t start, std::size_t& end) {
    int labels = 0, digits = 0;
    bool compressed = false, ipv4 = false, prefix = false, need = true;
    std::size_t part = 1;
    if (start >= end)
        return false;
    if (s[start] == u':' && (start + 1 >= end || s[start + 1] != u':'))
        return false;
    for (auto i = start; i < end; ++i) {
        if (prefix ? digit(s[i]) : hex(s[i]) >= 0) {
            ++digits;
            need = false;
            continue;
        }
        if (digits > 4)
            return false;
        if (digits) {
            ++labels;
            part = i - digits;
        }
        auto c = s[i];
        if (c == u'%') {
            do {
                if (++i == end)
                    return false;
                if (s[i] == u']')
                    break;
            } while (s[i] != u'/');
            c = s[i];
        }
        if (c == u']') {
            start = i;
            i = end;
            continue;
        }
        if (c == u'/') {
            if (labels == 0 || prefix)
                return false;
            prefix = true;
            need = true;
            digits = 0;
            continue;
        }
        if (c == u'.') {
            if (ipv4)
                return false;
            auto stop = end;
            if (!ipv4_canonical(s, part, stop))
                return false;
            ++labels;
            ipv4 = true;
            i = stop - 1;
        } else if (c == u':') {
            if (i > 0 && s[i - 1] == u':') {
                if (compressed)
                    return false;
                compressed = true;
                need = false;
            } else
                need = true;
        } else
            return false;
        digits = 0;
    }
    // The source uses the loop's end+1 sentinel after seeing a closing bracket.
    if (prefix && (digits < 1 || digits > 2))
        return false;
    int expected = 8 + (prefix ? 1 : 0);
    if (!need && digits <= 4 && (compressed ? labels < expected : labels == expected) && start < end &&
        s[start] == u']') {
        end = start + 1;
        return true;
    }
    return false;
}
bool ipv4_noncanonical(V s, std::size_t start, std::size_t& end) {
    std::uint64_t parts[4]{}, value = 0;
    int used = 0;
    bool had = false;
    auto i = start;
    for (; i < end; ++i) {
        int base = 10;
        value = 0;
        auto c = s[i];
        if (c == u'0') {
            base = 8;
            ++i;
            had = true;
            if (i < end && (s[i] == u'x' || s[i] == u'X')) {
                base = 16;
                ++i;
                had = false;
            }
        }
        for (; i < end; ++i) {
            c = s[i];
            int d = (base == 10 || base == 16) && digit(c) ? c - u'0'
                    : base == 8 && c >= u'0' && c <= u'7'  ? c - u'0'
                    : base == 16                           ? hex(c)
                                                           : -1;
            if (d < 0)
                break;
            value = value * base + d;
            if (value > 0xffffffffu)
                return false;
            had = true;
        }
        if (i >= end || s[i] != u'.')
            break;
        if (used >= 3 || !had || value > 255)
            return false;
        parts[used++] = value;
        had = false;
    }
    if (!had)
        return false;
    if (i < end && V(u"/\\:?#").find(s[i]) == V::npos)
        return false;
    if ((used == 1 && value > 0xffffff) || (used == 2 && value > 0xffff) || (used == 3 && value > 255))
        return false;
    end = i;
    return true;
}
bool dns(V s, std::size_t start, std::size_t& end) {
    auto stop = start;
    for (; stop < end; ++stop) {
        auto c = s[stop];
        bool invalid = c <= 159 && !letter(c) && !digit(c) && c != u'-' && c != u'_' && c != u'.';
        if (invalid) {
            if (V(u"/\\:?#").find(c) == V::npos)
                return false;
            break;
        }
    }
    if (stop == start)
        return false;
    auto host = s.substr(start, stop - start);
    std::size_t at = 0;
    while (at < host.size()) {
        auto first = host[at];
        if (first < 160 && !letter(first) && !digit(first))
            return false;
        auto dot = host.find_first_of(u".。．｡", at);
        auto length = dot == V::npos ? host.size() - at : dot - at;
        std::size_t budget = length;
        bool ascii = true;
        for (std::size_t n = at; n < at + length; ++n)
            ascii &= host[n] < 128;
        if (!ascii) {
            budget += 4;
            for (std::size_t n = at; n < at + length; ++n)
                if (host[n] > 255)
                    ++budget;
        }
        if (budget < 1 || budget > 63)
            return false;
        if (dot == V::npos)
            break;
        at = dot + 1;
    }
    end = stop;
    return true;
}
bool char_letter(char16_t c) {
    auto p =
        std::lower_bound(viewer_unicode::letters.begin(), viewer_unicode::letters.end(), std::uint32_t(c),
                         [](const auto& range, std::uint32_t value) { return range[1] < value; });
    return p != viewer_unicode::letters.end() && (*p)[0] <= c;
}
bool unc(V s, std::size_t start, std::size_t& end) {
    auto stop = end;
    if (start == stop)
        return false;
    bool had = false;
    auto i = start;
    for (; i < stop; ++i) {
        auto c = s[i];
        if (V(u"/\\:?#").find(c) != V::npos) {
            stop = i;
            break;
        }
        if (c == u'.') {
            ++i;
            break;
        }
        if (char_letter(c) || c == u'-' || c == u'_')
            had = true;
        else if (!digit(c))
            return false;
    }
    if (!had)
        return false;
    for (; i < stop; ++i) {
        auto c = s[i];
        if (V(u"/\\:?#").find(c) != V::npos) {
            stop = i;
            break;
        }
        if (c == u'.') {
            if (!had || (i > start && s[i - 1] == u'.'))
                return false;
            had = false;
            continue;
        }
        if (c == u'-' || c == u'_') {
            if (!had)
                return false;
            continue;
        }
        if (char_letter(c) || digit(c)) {
            had = true;
            continue;
        }
        return false;
    }
    if (i > start && s[i - 1] == u'.')
        had = true;
    if (!had)
        return false;
    end = stop;
    return true;
}
struct Normalizer {
    using Get = const void* (*)(std::int32_t*);
    using Normalize = std::int32_t (*)(const void*, const char16_t*, std::int32_t, char16_t*, std::int32_t,
                                       std::int32_t*);
    Get get = nullptr;
    Normalize normalize = nullptr;
#ifdef _WIN32
    HMODULE module = nullptr;
    void* symbol(const std::string& n) {
        return reinterpret_cast<void*>(GetProcAddress(module, n.c_str()));
    }
#else
    void* module = nullptr;
    void* symbol(const std::string& n) {
        return dlsym(module, n.c_str());
    }
#endif
    Normalizer() {
#ifdef _WIN32
        module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#elif defined(__APPLE__)
        module = dlopen("/usr/lib/libicucore.A.dylib", RTLD_LAZY | RTLD_LOCAL);
#else
        module = dlopen("libicui18n.so", RTLD_LAZY | RTLD_LOCAL);
        for (int n = 120; !module && n >= 40; --n)
            module = dlopen(("libicui18n.so." + std::to_string(n)).c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
        if (!module)
            return;
        std::string suffix;
        if (!symbol("unorm2_getNFCInstance"))
            for (int n = 120; n >= 40; --n)
                if (symbol("unorm2_getNFCInstance_" + std::to_string(n))) {
                    suffix = "_" + std::to_string(n);
                    break;
                }
        get = reinterpret_cast<Get>(symbol("unorm2_getNFCInstance" + suffix));
        normalize = reinterpret_cast<Normalize>(symbol("unorm2_normalize" + suffix));
    }
    ~Normalizer() {
#ifdef _WIN32
        if (module)
            FreeLibrary(module);
#else
        if (module)
            dlclose(module);
#endif
    }
    std::u16string apply(V s) {
        if (!get || !normalize)
            throw std::runtime_error(
                "Selected ICU normalization is required for configuration URI IRI hosts");
        std::int32_t status = 0;
        auto form = get(&status);
        if (status > 0 || !form)
            throw std::runtime_error("Cannot create selected ICU NFC normalizer");
        status = 0;
        auto count = normalize(form, s.data(), std::int32_t(s.size()), nullptr, 0, &status);
        if (status != 15 && status > 0)
            throw std::runtime_error("Cannot measure selected ICU NFC normalization");
        std::u16string out(count, u'\0');
        status = 0;
        count = normalize(form, s.data(), std::int32_t(s.size()), out.data(), count, &status);
        if (status > 0)
            throw std::runtime_error("Selected ICU NFC normalization failed");
        out.resize(count);
        return out;
    }
};
}
ConfigUriUnicodeBackend native_config_uri_unicode_backend() {
    ConfigUriUnicodeBackend b;
    b.normalize_nfc = [](V s) -> std::optional<std::u16string> {
        if (!valid_unicode(s))
            return {};
        if (std::all_of(s.begin(), s.end(), [](char16_t c) { return c < 128; }))
            return std::u16string(s);
        static Normalizer n;
        return n.apply(s);
    };
    return b;
}
bool is_http_configuration_uri(ConfigUriText input, const ConfigUriUnicodeBackend& unicode) {
    if (!input || input->empty() || input->size() >= 65520)
        return false;
    V s = *input;
    std::size_t begin = 0;
    while (begin < s.size() && lws(s[begin]))
        ++begin;
    auto colon = s.find(u':', begin);
    if (colon == V::npos)
        return false;
    auto scheme = s.substr(begin, colon - begin);
    auto match = [](V a, V b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            auto c = a[i];
            if (c >= u'A' && c <= u'Z')
                c += 32;
            if (c != b[i])
                return false;
        }
        return true;
    };
    if (!match(scheme, u"http") && !match(scheme, u"https"))
        return false;
    std::size_t end = s.size();
    while (end > colon + 1 && lws(s[end - 1]))
        --end;
    auto slash = [](char16_t c) { return c == u'/' || c == u'\\'; };
    auto authority = colon + 1;
    if (authority + 2 > end || !slash(s[authority]) || !slash(s[authority + 1]))
        return false;
    authority += 2;
    if (authority == end || s[authority] == u'/' || s[authority] == u'?' || s[authority] == u'#')
        return false;
    auto host = authority;
    for (auto i = authority; i < end; ++i) {
        if (i == end - 1 || V(u"?#\\/").find(s[i]) != V::npos)
            break;
        if (s[i] == u'@') {
            host = i + 1;
            break;
        }
    }
    auto stop = end;
    bool ip6 = false, numeric = false, unc_host = false;
    if (s[host] == u'[') {
        ip6 = ipv6(s, host + 1, stop);
        if (!ip6)
            return false;
    } else {
        numeric = digit(s[host]) && ipv4_noncanonical(s, host, stop);
        if (!numeric) {
            stop = end;
            if (!dns(s, host, stop)) {
                stop = end;
                if (!unc(s, host, stop) || stop - host > 256)
                    return false;
                unc_host = true;
            }
        }
    }
    if (stop < end && s[stop] == u'\\')
        return false;
    auto rest = stop;
    if (rest < end && s[rest] == u':') {
        std::uint32_t port = 0;
        for (++rest; rest < end; ++rest) {
            auto c = s[rest];
            if (digit(c)) {
                port = port * 10 + c - u'0';
                if (port > 65535)
                    return false;
            } else if (c == u'/' || c == u'?' || c == u'#')
                break;
            else
                return false;
        }
    }
    if (!iri_trigger(s))
        return true;
    std::size_t length = authority;
    if (host != authority) {
        length += iri_length(s.substr(authority, host - authority), false);
        if (length > 65535)
            return false;
    }
    if (ip6 || numeric || unc_host)
        length += stop - host;
    else {
        if (!unicode.normalize_nfc)
            throw std::invalid_argument("Configuration URI requires NFC normalization provider");
        std::u16string clean;
        for (auto c : s.substr(host, stop - host))
            if (!bidi(c))
                clean += c;
        auto normalized = unicode.normalize_nfc(clean);
        if (!normalized)
            return false;
        length += normalized->size();
    }
    length += rest - stop;
    auto pathend = s.find_first_of(u"?#", rest);
    if (pathend == V::npos)
        pathend = s.size();
    length += iri_length(s.substr(rest, pathend - rest), false);
    if (length > 65535)
        return false;
    if (pathend < s.size() && s[pathend] == u'?') {
        auto frag = s.find(u'#', pathend + 1);
        auto queryend = frag == V::npos ? s.size() : frag;
        length += iri_length(s.substr(pathend, queryend - pathend), true);
        if (length > 65535)
            return false;
        pathend = queryend;
    }
    if (pathend < s.size()) {
        length += iri_length(s.substr(pathend), false);
        if (length > 65535)
            return false;
    }
    return true;
}
bool is_http_configuration_uri(ConfigUriText input) {
    static auto backend = native_config_uri_unicode_backend();
    return is_http_configuration_uri(std::move(input), backend);
}
}

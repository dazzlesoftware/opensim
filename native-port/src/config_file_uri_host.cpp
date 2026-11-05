// Source .NET8.0.31 Uri.CheckAuthorityHelper for built-in file syntax.
// Copyright .NET Foundation and Contributors; MIT .NET license.
#include "opensim/framework/config_file_uri_host.hpp"
#include "opensim/framework/viewer_unicode_data.hpp"
#include <algorithm>
#include <stdexcept>
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
bool ipv4_noncanonical(V s, std::size_t start, std::size_t& end, bool not_implicit) {
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
    if (i < end && !(s[i] == u'/' || s[i] == u'\\' || (not_implicit && V(u":?#").find(s[i]) != V::npos)))
        return false;
    if ((used == 1 && value > 0xffffff) || (used == 2 && value > 0xffff) || (used == 3 && value > 255))
        return false;
    end = i;
    return true;
}
bool dns(V s, std::size_t start, std::size_t& end, bool not_implicit) {
    auto stop = start;
    for (; stop < end; ++stop) {
        auto c = s[stop];
        bool invalid = c <= 159 && !letter(c) && !digit(c) && c != u'-' && c != u'_' && c != u'.';
        if (invalid) {
            if (!(c == u'/' || c == u'\\' || (not_implicit && V(u":?#").find(c) != V::npos)))
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
bool unc(V s, std::size_t start, std::size_t& end, bool not_implicit) {
    auto stop = end;
    if (start == stop)
        return false;
    bool had = false;
    auto i = start;
    for (; i < stop; ++i) {
        auto c = s[i];
        if (c == u'/' || c == u'\\' || (not_implicit && V(u":?#").find(c) != V::npos)) {
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
        if (c == u'/' || c == u'\\' || (not_implicit && V(u":?#").find(c) != V::npos)) {
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
}
ConfigFileUriHostResult config_file_uri_check_host(std::u16string_view text, std::size_t start,
                                                   std::size_t length, std::uint64_t flags,
                                                   const ConfigUriUnicodeBackend& unicode) {
    ConfigFileUriHostResult r;
    r.end = start;
    r.flags = flags;
    bool implicit = (flags & 0x20000000ull) != 0, has_unicode = (flags & 0x200000000ull) != 0,
         rewrite = has_unicode && (flags & 0x400000000ull) == 0;
    if (rewrite)
        r.rewritten_prefix = std::u16string(text.substr(0, start));
    if (start == length || text[start] == u'/' || text[start] == u'\\' || text[start] == u'#' ||
        text[start] == u'?') {
        r.flags &= ~0x10000000ull;
        if (implicit)
            r.error = 8;
        else
            r.flags |= 0x50000;
        if (rewrite)
            r.flags |= 0x400000000ull;
        return r;
    }
    auto stop = length;
    bool recognized = false;
    if (text[start] == u'[' && ipv6(text, start + 1, stop)) {
        r.flags |= 0x10000;
        recognized = true;
    } else if (digit(text[start]) && ipv4_noncanonical(text, start, stop, !implicit)) {
        r.flags |= 0x20000;
        recognized = true;
    } else {
        stop = length;
        if (dns(text, start, stop, !implicit)) {
            r.flags |= 0x30000;
            r.flags |= 0x400000000ull;
            recognized = true;
            if (has_unicode) {
                if (!unicode.normalize_nfc)
                    throw std::invalid_argument("File URI requires exact NFC normalization");
                std::u16string clean;
                for (auto c : text.substr(start, stop - start))
                    if (!bidi(c))
                        clean += c;
                auto normalized = unicode.normalize_nfc(clean);
                if (normalized) {
                    if (!r.rewritten_prefix)
                        r.rewritten_prefix = std::u16string();
                    *r.rewritten_prefix += *normalized;
                } else
                    r.error = 8;
            }
        } else {
            stop = length;
            if (unc(text, start, stop, !implicit) && stop - start <= 256) {
                r.flags |= 0x40000;
                recognized = true;
            }
        }
    }
    if (recognized && rewrite && (r.flags & 0x400000000ull) == 0) {
        *r.rewritten_prefix += text.substr(start, stop - start);
        r.flags |= 0x400000000ull;
    }
    if (stop < length && text[stop] == u':')
        r.flags &= ~0x70000ull;
    if ((r.flags & 0x70000ull) == 0) {
        r.flags &= ~0x200000ull;
        r.flags |= 0x70000ull;
        r.error = 8;
        r.end = start;
        return r;
    }
    r.end = stop;
    return r;
}
}

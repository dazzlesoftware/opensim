// Source .NET8.0.31 Uri.PrivateParseMinimal file syntax; MIT .NET license.
#include "opensim/framework/config_file_uri_minimal.hpp"
namespace opensim::framework {
ConfigFileUriMinimalResult config_file_uri_parse_minimal(std::u16string text, std::u16string_view original,
                                                         std::uint64_t flags, bool windows, bool unix_syntax,
                                                         const ConfigUriUnicodeBackend& unicode) {
    ConfigFileUriMinimalResult r;
    r.flags = flags;
    r.parse_string = std::move(text);
    r.unix_file_syntax = unix_syntax;
    auto input = (flags & 0x400000000ull) == 0 ? original : std::u16string_view(r.parse_string);
    std::size_t num = flags & 65535, length = r.parse_string.size();
    r.flags &= ~0x100ffffull;
    auto sep = [](char16_t c) { return c == u'/' || c == u'\\'; };
    auto alpha = [](char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); };
    auto lws = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; };
    while (length > num && lws(input[length - 1]))
        --length;
    if (!windows && (r.flags & 0x100000000000ull)) {
        r.flags |= 0x50000ull | num;
        return r;
    }
    if ((r.flags & 0x20000000ull) == 0 && num + 1 < length) {
        auto i = num;
        while (i < length && sep(input[i]))
            ++i;
        if (i - num >= 2)
            r.flags |= 0x100000ull;
        if (i + 1 < length && (input[i + 1] == u':' || input[i + 1] == u'|') && alpha(input[i])) {
            if (i + 2 >= length || !sep(input[i + 2])) {
                r.error = 7;
                return r;
            }
            r.flags |= 0x8100000ull;
            num = (i == num || i - num == 2) ? i : i - 1;
        } else if (i - num >= 2 && i - num != 3 && i < length && input[i] != u'?' && input[i] != u'#') {
            r.flags |= 0x10000000ull;
            num = i;
        } else if (!windows && i - num == 3 && input[i - 1] == u'/') {
            r.unix_file_syntax = true;
            r.flags |= 0x100000100000ull;
            num += 2;
        }
    }
    if ((r.flags & 0x100018000000ull) == 0) {
        if (num + 2 <= length) {
            if (!sep(input[num]) || !sep(input[num + 1])) {
                r.error = 3;
                return r;
            }
            r.flags |= 0x100000ull;
            num += 2;
        } else {
            r.error = 3;
            return r;
        }
    }
    if (r.flags & 0x8000000ull) {
        r.flags |= ((r.flags & 0x100000ull) ? 0x50000ull : 0x70000ull) | num;
        return r;
    }
    auto host = config_file_uri_check_host(input, num, length, r.flags, unicode);
    r.flags = host.flags;
    num = host.end;
    if (host.error) {
        r.error = host.error;
        return r;
    }
    if (num < length && !windows && input[num] == u'/' && (r.flags & 0x20000000ull) == 0 &&
        (r.flags & 0x10000000ull) && !r.unix_file_syntax)
        r.unix_file_syntax = true;
    r.flags |= num;
    if ((flags & 0x200000000ull) && host.rewritten_prefix)
        r.parse_string = *host.rewritten_prefix;
    return r;
}
}

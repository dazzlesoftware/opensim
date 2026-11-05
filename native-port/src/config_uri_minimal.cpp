// Native .NET8.0.31 Uri.PrivateParseMinimal; .NET Foundation MIT license.
#include "opensim/framework/config_uri_minimal.hpp"
#include <stdexcept>
namespace opensim::framework {
ConfigUriMinimalResult config_uri_parse_minimal(std::u16string text, std::u16string_view original,
                                                std::uint64_t flags, ConfigUriSyntaxPtr syntax, bool windows,
                                                const ConfigUriUnicodeBackend& unicode) {
    if (!syntax)
        throw std::invalid_argument("Minimal URI parsing requires validated syntax");
    ConfigUriMinimalResult r;
    r.flags = flags;
    r.parse_string = std::move(text);
    r.syntax = std::move(syntax);
    auto input = (flags & 0x400000000ull) ? std::u16string_view(r.parse_string) : original;
    std::size_t num = flags & 0xffffull, length = r.parse_string.size();
    if (num > length || length > input.size())
        throw std::out_of_range("Minimal URI parsing requires validated string/cursor bounds");
    r.flags &= ~0x100ffffull;
    using F = ConfigUriSyntaxFlags;
    auto has = [&](F f) { return (r.syntax->flags & static_cast<std::uint64_t>(f)) != 0; };
    auto sep = [](char16_t c) { return c == u'/' || c == u'\\'; };
    auto alpha = [](char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); };
    auto lws = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; };
    if (length > num && lws(input[length - 1])) {
        --length;
        while (length != num && lws(input[--length])) {
        }
        ++length;
    }
    if (!windows && (r.flags & 0x100000000000ull)) {
        r.flags |= 0x50000ull | num;
        return r;
    }
    if (has(F::AllowEmptyHost) && has(F::AllowDOSPath) && !(r.flags & 0x20000000ull) && num + 1 < length) {
        auto i = num;
        while (i < length && sep(input[i]))
            ++i;
        if (has(F::FileLikeUri) || i - num <= 3) {
            if (i - num >= 2)
                r.flags |= 0x100000ull;
            if (i + 1 < length && (input[i + 1] == u':' || input[i + 1] == u'|') && alpha(input[i])) {
                if (i + 2 >= length || !sep(input[i + 2])) {
                    if (has(F::FileLikeUri)) {
                        r.error = 7;
                        return r;
                    }
                } else {
                    r.flags |= 0x8000000ull;
                    if (has(F::MustHaveAuthority))
                        r.flags |= 0x100000ull;
                    num = (i == num || i - num == 2) ? i : i - 1;
                }
            } else if (has(F::FileLikeUri) && i - num >= 2 && i - num != 3 && i < length &&
                       input[i] != u'?' && input[i] != u'#') {
                r.flags |= 0x10000000ull;
                num = i;
            } else if (!windows && has(F::FileLikeUri) && i > 0 && input[i - 1] == u'/' && i - num == 3) {
                r.syntax = NativeConfigUriSyntaxCatalog::unix_file();
                r.flags |= 0x100000100000ull;
                num += 2;
            }
        }
    }
    if (!(r.flags & 0x100018000000ull)) {
        if (num + 2 <= length) {
            auto a = input[num], b = input[num + 1];
            if (has(F::MustHaveAuthority)) {
                if (!sep(a) || !sep(b)) {
                    r.error = 3;
                    return r;
                }
                r.flags |= 0x100000ull;
                num += 2;
            } else if (has(F::OptionalAuthority) && ((r.flags & 0x100000ull) || (a == u'/' && b == u'/'))) {
                r.flags |= 0x100000ull;
                num += 2;
            } else if (!has(F::MailToLikeUri)) {
                if ((r.flags & 0x600000000ull) == 0x200000000ull)
                    r.parse_string.resize(num);
                r.flags |= num | 0x70000ull;
                return r;
            }
        } else {
            if (has(F::MustHaveAuthority)) {
                r.error = 3;
                return r;
            }
            if (!has(F::MailToLikeUri)) {
                if ((r.flags & 0x600000000ull) == 0x200000000ull)
                    r.parse_string.resize(num);
                r.flags |= num | 0x70000ull;
                return r;
            }
        }
    }
    if (r.flags & 0x8000000ull) {
        r.flags |= ((r.flags & 0x100000ull) ? 0x50000ull : 0x70000ull) | num;
        return r;
    }
    auto authority = config_uri_check_authority(input, num, length, r.flags, *r.syntax, unicode);
    r.flags = authority.flags;
    num = authority.end;
    if (authority.error) {
        r.error = authority.error;
        return r;
    }
    if (num < length) {
        auto c = input[num];
        if (c == u'\\' && !(r.flags & 0x20000000ull) && !has(F::AllowDOSPath)) {
            r.error = 11;
            return r;
        }
        if (!windows && c == u'/' && !(r.flags & 0x20000000ull) && (r.flags & 0x10000000ull) &&
            r.syntax == native_config_uri_syntax_catalog().get(u"file"))
            r.syntax = NativeConfigUriSyntaxCatalog::unix_file();
    }
    r.flags |= num;
    if (has(F::AllowIriParsing) && authority.rewritten_prefix)
        r.parse_string = *authority.rewritten_prefix;
    return r;
}
}

// Source .NET8.0.31 Uri.CheckCanonical, general syntax flags; MIT .NET license.
#include "opensim/framework/config_uri_canonical.hpp"
namespace opensim::framework {
namespace {
int hex(char16_t c) {
    return c >= u'0' && c <= u'9'   ? c - u'0'
           : c >= u'A' && c <= u'F' ? c - u'A' + 10
           : c >= u'a' && c <= u'f' ? c - u'a' + 10
                                    : -1;
}
bool iri(std::uint32_t c) {
    if (c <= 65535)
        return (c >= 160 && c <= 55295) || (c >= 63744 && c <= 64975) || (c >= 65008 && c <= 65519) ||
               (c >= 57344 && c <= 63743);
    return (c & 65535) < 65534 && !(c >= 917504 && c <= 921599);
}
}
ConfigUriCanonicalResult config_uri_check_canonical(std::u16string_view text, std::size_t start,
                                                    std::size_t end, char16_t delimiter, bool implicit,
                                                    bool unicode, const ConfigUriSyntax* syntax) {
    auto has = [&](ConfigUriSyntaxFlags f) {
        return syntax && (syntax->flags & static_cast<std::uint64_t>(f)) != 0;
    };
    bool iri_parsing = !syntax || has(ConfigUriSyntaxFlags::AllowIriParsing);
    int check = 0;
    bool flag = false, flag2 = false;
    std::size_t i = start;
    for (; i < end; ++i) {
        auto c = text[i];
        if (c <= 31 || (c >= 127 && c <= 159)) {
            flag = flag2 = true;
            check |= 32;
            continue;
        }
        if (c > u'~') {
            if (iri_parsing) {
                bool valid = false;
                check |= 8;
                if (c >= 0xd800 && c <= 0xdbff) {
                    if (i + 1 < end) {
                        auto low = text[i + 1];
                        if (low >= 0xdc00 && low <= 0xdfff)
                            valid = iri(65536 + ((c - 0xd800) << 10) + (low - 0xdc00));
                    }
                } else
                    valid = iri(c);
                if (!valid)
                    check |= 64;
            }
            flag = true;
            continue;
        }
        if (c == delimiter || (delimiter == u'?' && c == u'#' && has(ConfigUriSyntaxFlags::MayHaveFragment)))
            break;
        if (c == u'?') {
            if (implicit ||
                (syntax && !has(ConfigUriSyntaxFlags::MayHaveQuery) && delimiter != char16_t(0xfffe))) {
                check |= 32;
                flag2 = flag = true;
            }
            continue;
        }
        if (c == u'#') {
            flag = true;
            if (implicit || (syntax && !has(ConfigUriSyntaxFlags::MayHaveFragment))) {
                check |= 32;
                flag2 = true;
            }
            continue;
        }
        if (c == u'/' || c == u'\\') {
            if (c == u'\\')
                check |= 16;
            if (!(check & 4) && i + 1 != end && (text[i + 1] == u'/' || text[i + 1] == u'\\'))
                check |= 4;
            continue;
        }
        if (c == u'.') {
            auto next = i + 1 < text.size() ? text[i + 1] : char16_t(0);
            if ((!(check & 4) && i + 1 == end) || next == u'.' || next == u'/' || next == u'\\' ||
                next == u'?' || next == u'#')
                check |= 4;
            continue;
        }
        if ((c > u'"' || c == u'!') && (c < u'[' || c > u'^')) {
            if (c == u'{' || c == u'|' || c == u'}') {
                flag = true;
                continue;
            }
            if (c != u'<' && c != u'>' && c != u'`') {
                if (c != u'%')
                    continue;
                flag2 = true;
                const bool has_escape_digits = i + 2 < end;
                const int high_digit = has_escape_digits ? hex(text[i + 1]) : -1;
                const int low_digit = has_escape_digits ? hex(text[i + 2]) : -1;
                if (high_digit >= 0 && low_digit >= 0) {
                    c = static_cast<char16_t>((high_digit << 4) | low_digit);
                    if (c == u'.' || c == u'/' || c == u'\\')
                        check |= 128;
                    i += 2;
                } else
                    flag = true;
                continue;
            }
        }
        flag = true;
        if (unicode)
            check |= 64;
    }
    if (flag2) {
        if (!flag)
            check |= 1;
    } else {
        check |= 2;
        if (!flag)
            check |= 1;
    }
    return {check, i};
}
}

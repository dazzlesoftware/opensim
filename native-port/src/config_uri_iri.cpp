// .NET Foundation IriHelper, MIT license; reusable URI constructor kernel.
#include "opensim/framework/config_uri_iri.hpp"
#include "opensim/framework/config_uri_unescape.hpp"
namespace opensim::framework {
namespace {
int hex(char16_t c) {
    return c >= u'0' && c <= u'9'   ? c - u'0'
           : c >= u'A' && c <= u'F' ? c - u'A' + 10
           : c >= u'a' && c <= u'f' ? c - u'a' + 10
                                    : -1;
}
bool letter(char16_t c) {
    return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z');
}
bool iri(std::uint32_t c, bool query) {
    if (c <= 65535)
        return (c >= 160 && c <= 55295) || (c >= 63744 && c <= 64975) || (c >= 65008 && c <= 65519) ||
               (query && c >= 57344 && c <= 63743);
    return (c & 65535) < 65534 && !(c >= 917504 && c <= 921599) && (query || c < 983040);
}
bool unsafe(char16_t c) {
    return c <= 31 || (c >= 127 && c <= 159) ||
           std::u16string_view(u";/?:@&=+$,#[]!'()*%\\").find(c) != std::u16string_view::npos;
}
bool reserved(char16_t c, std::uint32_t component) {
    if (!(component & 127))
        return component == 0 && std::u16string_view(u":/?#[]@").find(c) != std::u16string_view::npos;
    return std::u16string_view(u";/?:@&=+$,#[]!'()*").find(c) != std::u16string_view::npos;
}
void pct(std::u16string& out, std::uint32_t b) {
    out += u'%';
    out += u"0123456789ABCDEF"[(b >> 4) & 15];
    out += u"0123456789ABCDEF"[b & 15];
}
void encode(std::u16string& out, std::uint32_t c) {
    if (c < 128)
        pct(out, c);
    else if (c < 2048) {
        pct(out, 0xc0 | (c >> 6));
        pct(out, 0x80 | (c & 63));
    } else if (c < 65536) {
        pct(out, 0xe0 | (c >> 12));
        pct(out, 0x80 | ((c >> 6) & 63));
        pct(out, 0x80 | (c & 63));
    } else {
        pct(out, 0xf0 | (c >> 18));
        pct(out, 0x80 | ((c >> 12) & 63));
        pct(out, 0x80 | ((c >> 6) & 63));
        pct(out, 0x80 | (c & 63));
    }
}
}
bool config_uri_has_iri_characters(std::u16string_view input) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        auto c = input[i];
        if (c == u'%') {
            if (i + 2 < input.size()) {
                auto a = hex(input[i + 1]), b = hex(input[i + 2]);
                auto value = a < 0 || b < 0 ? 0xffff : a * 16 + b;
                if (value > 127 || letter(char16_t(value)) || (value >= u'0' && value <= u'9') ||
                    std::u16string_view(u"-._~").find(char16_t(value)) != std::u16string_view::npos)
                    return true;
                i += 2;
            }
        } else if (c > 127)
            return true;
    }
    return false;
}
std::u16string config_uri_escape_unescape_iri(std::u16string_view input, std::uint32_t component) {
    std::u16string out;
    out.reserve(input.size());
    bool query = component == 32;
    for (std::size_t i = 0; i < input.size(); ++i) {
        auto c = input[i];
        if (c == u'%') {
            if (i + 2 < input.size()) {
                auto a = hex(input[i + 1]), b = hex(input[i + 2]);
                char16_t value = a < 0 || b < 0 ? char16_t(0xffff) : char16_t(a * 16 + b);
                if (value == 0xffff || value == u'%' || reserved(value, component) || unsafe(value)) {
                    out.append(input.substr(i, 3));
                    i += 2;
                } else if (value <= 127) {
                    out += value;
                    i += 2;
                } else {
                    auto end = i;
                    while (end + 2 < input.size() && input[end] == u'%' && hex(input[end + 1]) >= 8 &&
                           hex(input[end + 2]) >= 0)
                        end += 3;
                    out +=
                        config_uri_unescape(input.substr(i, end - i), 0xffff, 0xffff, 0xffff, 3, true, query);
                    i = end - 1;
                }
            } else
                out += c;
        } else if (c > 127) {
            std::uint32_t scalar = c;
            bool pair = c >= 0xd800 && c <= 0xdbff && i + 1 < input.size() && input[i + 1] >= 0xdc00 &&
                        input[i + 1] <= 0xdfff;
            if (pair)
                scalar = 0x10000 + ((c - 0xd800) << 10) + (input[i + 1] - 0xdc00);
            if (iri(scalar, query)) {
                out += c;
                if (pair)
                    out += input[i + 1];
            } else {
                if (!pair && c >= 0xd800 && c <= 0xdfff)
                    scalar = 0xfffd;
                encode(out, scalar);
            }
            if (pair)
                ++i;
        } else
            out += c;
    }
    return out;
}
}

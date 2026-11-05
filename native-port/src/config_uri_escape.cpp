// Source .NET8.0.31 UriHelper.EscapeString/EscapeStringToBuilder. MIT .NET license.
#include "opensim/framework/config_uri_escape.hpp"
namespace opensim::framework {
namespace {
std::u16string_view allowed(ConfigUriEscapeSet set) {
    switch (set) {
    case ConfigUriEscapeSet::Unreserved:
        return u"-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~";
    case ConfigUriEscapeSet::UnreservedReserved:
        return u"!#$&'()*+,-./0123456789:;=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_abcdefghijklmnopqrstuvwxyz~";
    case ConfigUriEscapeSet::ExceptHash:
        return u"!$&'()*+,-./0123456789:;=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_abcdefghijklmnopqrstuvwxyz~";
    case ConfigUriEscapeSet::ExceptQuestionMarkHash:
        return u"!$&'()*+,-./0123456789:;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_abcdefghijklmnopqrstuvwxyz~";
    }
    throw std::invalid_argument("Invalid consumed URI escape character set");
}
bool hex(char16_t c) {
    return (c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'F') || (c >= u'a' && c <= u'f');
}
void byte(std::u16string& out, std::uint32_t c) {
    out += u'%';
    out += u"0123456789ABCDEF"[(c >> 4) & 15];
    out += u"0123456789ABCDEF"[c & 15];
}
void scalar(std::u16string& out, std::uint32_t c) {
    if (c < 128)
        byte(out, c);
    else if (c < 2048) {
        byte(out, 0xc0 | (c >> 6));
        byte(out, 0x80 | (c & 63));
    } else if (c < 65536) {
        byte(out, 0xe0 | (c >> 12));
        byte(out, 0x80 | ((c >> 6) & 63));
        byte(out, 0x80 | (c & 63));
    } else {
        byte(out, 0xf0 | (c >> 18));
        byte(out, 0x80 | ((c >> 12) & 63));
        byte(out, 0x80 | ((c >> 6) & 63));
        byte(out, 0x80 | (c & 63));
    }
}
}
std::u16string config_uri_escape(std::u16string_view text, bool existing, ConfigUriEscapeSet set) {
    auto safe = allowed(set);
    std::u16string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = std::uint32_t(text[i]);
        if (c >= 128) {
            if (c >= 0xd800 && c <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 &&
                text[i + 1] <= 0xdfff)
                c = 65536 + ((c - 0xd800) << 10) + (text[++i] - 0xdc00);
            else if (c >= 0xd800 && c <= 0xdfff)
                c = 65533;
            scalar(out, c);
        } else if (safe.find(static_cast<char16_t>(c)) != safe.npos)
            out += static_cast<char16_t>(c);
        else if (c == u'%' && existing && i + 2 < text.size() && hex(text[i + 1]) && hex(text[i + 2])) {
            out.append(text.substr(i, 3));
            i += 2;
        } else
            byte(out, c);
    }
    return out;
}
ConfigUriText config_uri_escape(ConfigUriText input, bool existing, ConfigUriEscapeSet set) {
    if (!input)
        throw ConfigUriEscapeError();
    auto safe = allowed(set);
    if (input->find_first_not_of(safe) == input->npos)
        return input;
    return std::make_shared<const std::u16string>(
        config_uri_escape(std::u16string_view(*input), existing, set));
}
}

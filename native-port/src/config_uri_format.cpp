// Source .NET8.0.31 Uri.GetCanonicalPath, general syntax; MIT .NET license.
#include "opensim/framework/config_uri_format.hpp"
#include "opensim/framework/config_file_uri_path.hpp"
#include "opensim/framework/config_uri_escape.hpp"
#include "opensim/framework/config_uri_unescape.hpp"
namespace opensim::framework {
std::u16string config_uri_canonical_path(ConfigUriState& s, std::u16string dest, std::int32_t format,
                                         bool windows) {
    config_uri_parse_remaining(s);
    auto flags = s.flags;
    auto& text = s.parse_string;
    auto o = s.offsets;
    auto has = [&](ConfigUriSyntaxFlags f) { return (s.syntax->flags & static_cast<std::uint64_t>(f)) != 0; };
    bool implicit = (flags & 0x20000000) != 0, convert = has(ConfigUriSyntaxFlags::ConvertPathSlashes),
         user = (flags & 0x80000) != 0, compress = (flags & 0x2000) != 0, pathbad = (flags & 16) != 0,
         escapedbad = (flags & 1024) != 0;
    if (flags & 0x4000)
        dest += u'/';
    if (o.path == o.query)
        return dest;
    std::size_t length = dest.size(), secured = 0;
    if (flags & 0x8000000)
        secured = (text[o.path] == u'/' || text[o.path] == u'\\') ? 3 : 2;
    auto raw = std::u16string_view(text).substr(o.path, o.query - o.path);
    auto unescape_dots = [&] {
        auto tail = config_file_uri_unescape_dots(dest.substr(length), convert);
        dest.resize(length);
        dest += tail;
    };
    if (format == 1) {
        if (compress) {
            dest += raw;
            if (has(ConfigUriSyntaxFlags::UnEscapeDotsAndSlashes) && pathbad && !implicit)
                unescape_dots();
        } else if (escapedbad && !user) {
            std::u16string changed(text);
            if (secured && changed[secured + o.path - 1] == u'|')
                changed[secured + o.path - 1] = u':';
            dest += config_uri_escape(std::u16string_view(changed).substr(o.path, o.query - o.path),
                                      !implicit, ConfigUriEscapeSet::ExceptQuestionMarkHash);
        } else
            dest += raw;
        if (!windows && (flags & 0x8000) && !convert && has(ConfigUriSyntaxFlags::FileLikeUri) && !implicit) {
            auto escaped = config_uri_escape(std::u16string_view(dest).substr(length), true,
                                             ConfigUriEscapeSet::UnreservedReserved);
            dest.resize(length);
            dest += escaped;
            length = dest.size();
        }
    } else {
        dest += raw;
        if (compress && has(ConfigUriSyntaxFlags::UnEscapeDotsAndSlashes) && pathbad && !implicit)
            unescape_dots();
    }
    auto num = length + secured;
    if (secured && dest[num - 1] == u'|')
        dest[num - 1] = u':';
    if (compress && dest.size() > num) {
        auto compressed = config_file_uri_compress(dest.substr(num), convert,
                                                   has(ConfigUriSyntaxFlags::CanonicalizeAsFilePath));
        dest.resize(num);
        dest += compressed;
        if (length < dest.size() && dest[length] == u'\\')
            dest[length] = u'/';
        if (format == 1 && !user && escapedbad) {
            auto escaped = config_uri_escape(std::u16string_view(dest).substr(length), !implicit,
                                             ConfigUriEscapeSet::ExceptQuestionMarkHash);
            dest.resize(length);
            dest += escaped;
            length = dest.size();
        }
    }
    if (format == 1 || !pathbad)
        return dest;
    int mode;
    if (format == 32767) {
        mode = (user ? 2 : 3) | 4;
        if (implicit)
            mode &= ~2;
    } else if (format == 2)
        mode = implicit ? 0 : 10;
    else {
        mode = user ? 2 : 3;
        if (implicit)
            mode &= ~2;
    }
    if (mode) {
        auto tail = config_uri_unescape(std::u16string_view(dest).substr(length), u'?', u'#', 0xffff, mode,
                                        has(ConfigUriSyntaxFlags::AllowIriParsing), false);
        dest.resize(length);
        dest += tail;
    }
    return dest;
}
}

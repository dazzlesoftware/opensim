// Native .NET8.0.31 Uri.CreateUriInfo; .NET Foundation MIT license.
#include "opensim/framework/config_uri_state.hpp"
#include "opensim/framework/config_uri_canonical.hpp"
#include "opensim/framework/config_uri_iri.hpp"
namespace opensim::framework {
void config_uri_create_info(ConfigUriState& s) {
    if (s.info)
        return;
    if (!s.syntax)
        throw std::invalid_argument("URI state requires validated syntax");
    auto cf = s.flags;
    ConfigUriOffsets o;
    o.end = static_cast<std::uint16_t>(s.parse_string.size());
    auto& text = s.parse_string;
    auto at = [&](std::size_t i) {
        if (i >= text.size())
            throw ConfigUriStateError(true);
        return text[i];
    };
    auto original = [&](std::size_t i) {
        if (i >= s.original_string.size())
            throw ConfigUriStateError(true);
        return s.original_string[i];
    };
    auto lws = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; };
    auto sep = [](char16_t c) { return c == u'/' || c == u'\\'; };
    if (!(cf & 0x1000000ull)) {
        bool changed_slashes = false;
        std::size_t i = 0;
        if (cf & 0x20000000ull) {
            while (lws(at(i))) {
                ++i;
                ++o.scheme;
            }
            if (cf & 0x10000000ull) {
                i += 2;
                auto stop = cf & 0xffffull;
                while (i < stop && sep(at(i)))
                    ++i;
            }
        } else {
            i = s.syntax->scheme->size();
            while (at(i++) != u':')
                ++o.scheme;
            if (cf & 0x100000ull) {
                if (at(i) == u'\\' || at(i + 1) == u'\\')
                    changed_slashes = true;
                i += 2;
                if (cf & (0x8000000ull | 0x10000000ull)) {
                    auto stop = cf & 0xffffull;
                    while (i < stop && sep(at(i))) {
                        ++i;
                        changed_slashes = true;
                    }
                }
            }
        }
        if (s.syntax->default_port != -1)
            o.port_value = static_cast<std::uint16_t>(s.syntax->default_port);
        if ((cf & 0x70000ull) == 0x70000ull || (cf & 0x8000000ull)) {
            o.user = o.host = o.path = static_cast<std::uint16_t>(cf & 0xffffull);
            cf &= ~0xffffull;
            if (changed_slashes)
                cf |= 1;
        } else {
            o.user = static_cast<std::uint16_t>(i);
            if ((s.flags & 0x70000ull) == 0x50000ull) {
                o.host = static_cast<std::uint16_t>(i);
                o.path = static_cast<std::uint16_t>(cf & 0xffffull);
                cf &= ~0xffffull;
            } else {
                if (cf & 0x200000ull) {
                    while (at(i) != u'@')
                        ++i;
                    ++i;
                }
                o.host = static_cast<std::uint16_t>(i);
                i = cf & 0xffffull;
                cf &= ~0xffffull;
                if (changed_slashes)
                    cf |= 1;
                o.path = static_cast<std::uint16_t>(i);
                bool had_port = false;
                if (cf & 0x200000000ull)
                    o.end = static_cast<std::uint16_t>(s.original_string.size());
                if (i < o.end && original(i) == u':') {
                    std::int32_t port = 0;
                    if (++i < o.end) {
                        port = std::int32_t(original(i)) - 48;
                        if (static_cast<std::uint32_t>(port) <= 9) {
                            had_port = true;
                            if (port == 0)
                                cf |= 0x208ull;
                            for (++i; i < o.end; ++i) {
                                auto digit = std::int32_t(original(i)) - 48;
                                if (static_cast<std::uint32_t>(digit) > 9)
                                    break;
                                port = port * 10 + digit;
                            }
                        }
                    }
                    if (had_port && s.syntax->default_port != port) {
                        o.port_value = static_cast<std::uint16_t>(port);
                        cf |= 0x800000ull;
                    } else
                        cf |= 0x208ull;
                    o.path = static_cast<std::uint16_t>(i);
                }
            }
        }
    }
    cf |= 0x40000000ull;
    s.offsets = o;
    s.info = true;
    s.flags = (s.flags & ~0xffffull) | cf;
}
void config_uri_parse_remaining(ConfigUriState& s) {
    if (s.flags & 0x80000000ull)
        return;
    config_uri_create_info(s);
    std::uint64_t flags = 0;
    using F = ConfigUriSyntaxFlags;
    auto has = [&](F f) { return (s.syntax->flags & static_cast<std::uint64_t>(f)) != 0; };
    auto lws = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; };
    auto sep = [](char16_t c) { return c == u'/' || c == u'\\'; };
    auto at = [&](std::size_t i) {
        if (i >= s.parse_string.size())
            throw ConfigUriStateError(true);
        return s.parse_string[i];
    };
    auto trim = [&](std::size_t& length, std::size_t index) {
        while (length > index && lws(at(length - 1)))
            --length;
    };
    if (!(s.flags & 0x1000000ull)) {
        bool normalize = (s.flags & 0xa00000000ull) == 0x200000000ull, iri = has(F::AllowIriParsing),
             implicit = (s.flags & 0x20000000ull) != 0;
        std::size_t position = s.offsets.scheme, length = s.parse_string.size();
        std::int32_t check = 0;
        trim(length, position);
        auto canonical = [&](std::size_t end, char16_t delimiter) {
            auto value = config_uri_check_canonical(s.parse_string, position, end, delimiter, implicit,
                                                    (s.flags & 0x200000000ull) != 0, s.syntax.get());
            position = value.position;
            check = value.check;
        };
        if (implicit)
            flags |= 1;
        else {
            auto& name = *s.syntax->scheme;
            for (std::size_t i = 0; i < name.size(); ++i)
                if (name[i] != at(position + i))
                    flags |= 1;
            if ((s.flags & 0x100000ull) &&
                (position + name.size() + 3 >= length || at(position + name.size() + 1) != u'/' ||
                 at(position + name.size() + 2) != u'/'))
                flags |= 1;
        }
        if (s.flags & 0x200000ull) {
            position = s.offsets.user;
            canonical(s.offsets.host, u'@');
            if (!(check & 2))
                flags |= 2;
            if ((check & (1 | 16)) != 1)
                flags |= 0x80;
            if (iri && (check & (1 | 2 | 16 | 64 | 8)) == (2 | 8))
                flags |= 0x8000000000ull;
        }
        position = s.offsets.path;
        std::size_t source = s.offsets.path;
        if (normalize) {
            if (s.syntax->scheme == native_config_uri_syntax_catalog().get(u"file")->scheme &&
                !(s.flags & 0x10000000ull))
                s.parse_string = implicit ? u"" : *s.syntax->scheme + u"://";
            s.offsets.path = static_cast<std::uint16_t>(s.parse_string.size());
            position = s.offsets.path;
        }
        if (s.flags & 0x200000000000ull) {
            if (normalize)
                s.parse_string += std::u16string_view(s.original_string).substr(source);
            if (implicit || !has(F::MayHaveQuery))
                position = s.parse_string.size();
            else {
                position = s.parse_string.find(u'?');
                if (position == s.parse_string.npos)
                    position = s.parse_string.size();
            }
            s.offsets.query = static_cast<std::uint16_t>(position);
            s.offsets.fragment = s.offsets.end = static_cast<std::uint16_t>(s.parse_string.size());
        } else {
            auto append_iri = [&](std::size_t start, std::size_t end, std::uint32_t component) {
                s.parse_string += config_uri_escape_unescape_iri(
                    std::u16string_view(s.original_string).substr(start, end - start), component);
                if (s.parse_string.size() > 65535)
                    throw ConfigUriStateError();
                length = s.parse_string.size();
            };
            if (normalize) {
                auto start = source;
                if (implicit || (!has(F::MayHaveQuery) && !has(F::MayHaveFragment)))
                    source = s.original_string.size();
                else {
                    source = has(F::MayHaveQuery)
                                 ? (has(F::MayHaveFragment) ? s.original_string.find_first_of(u"?#", source)
                                                            : s.original_string.find(u'?', source))
                                 : s.original_string.find(u'#', source);
                    if (source == s.original_string.npos)
                        source = s.original_string.size();
                }
                append_iri(start, source, 16);
                if (s.parse_string == s.original_string)
                    trim(length, position);
            }
            canonical(length, !implicit && (has(F::MayHaveQuery) || has(F::MayHaveFragment))
                                  ? (has(F::MayHaveQuery)      ? u'?'
                                     : has(F::MayHaveFragment) ? u'#'
                                                               : char16_t(0xfffe))
                                  : char16_t(0xffff));
            if ((s.flags & 0x100000ull) && has(F::PathIsRooted) &&
                (s.offsets.path == length || !sep(at(s.offsets.path))))
                flags |= 0x4000;
            bool bad = false;
            if ((s.flags & 0x8000000ull) ||
                ((s.flags & 0x100000ull) &&
                 (has(F::ConvertPathSlashes) || has(F::CompressPath) || has(F::UnEscapeDotsAndSlashes)))) {
                if ((check & 128) && has(F::UnEscapeDotsAndSlashes)) {
                    flags |= 0x410;
                    bad = true;
                }
                if (has(F::ConvertPathSlashes) && (check & 16)) {
                    flags |= 0x410;
                    bad = true;
                }
                if (has(F::CompressPath) && ((flags & 0x400) || (check & 4)))
                    flags |= 0x2000;
                if (check & 16)
                    flags |= 0x8000;
            } else if (check & 16) {
                flags |= 0x400;
                bad = true;
            }
            if (!(check & 2) && (!implicit || (s.flags & 0x80000ull) || (check & 32))) {
                flags |= 0x10;
                bad = true;
            }
            if (implicit && (check & (1 | 32)))
                check &= ~1;
            if (!(check & 1))
                flags |= 0x400;
            if (iri && !bad && (check & (1 | 2 | 64 | 8)) == (2 | 8))
                flags |= 0x10000000000ull;
            if (normalize) {
                auto start = source;
                if (source < s.original_string.size() && s.original_string[source] == u'?') {
                    if (has(F::MayHaveFragment)) {
                        source = s.original_string.find(u'#', source + 1);
                        if (source == s.original_string.npos)
                            source = s.original_string.size();
                    } else
                        source = s.original_string.size();
                    append_iri(start, source, 32);
                    if (s.parse_string == s.original_string)
                        trim(length, position);
                }
            }
            s.offsets.query = static_cast<std::uint16_t>(position);
            if (position < length && at(position) == u'?') {
                ++position;
                canonical(length, has(F::MayHaveFragment) ? u'#' : char16_t(0xfffe));
                if (!(check & 2))
                    flags |= 0x20;
                if ((check & (1 | 16)) != 1)
                    flags |= 0x800;
                if (iri && (check & (1 | 2 | 16 | 64 | 8)) == (2 | 8))
                    flags |= 0x20000000000ull;
            }
            if (normalize) {
                auto start = source;
                if (source < s.original_string.size() && s.original_string[source] == u'#') {
                    source = s.original_string.size();
                    append_iri(start, source, 64);
                    trim(length, position);
                }
            }
            s.offsets.fragment = static_cast<std::uint16_t>(position);
            if (position < length && at(position) == u'#') {
                ++position;
                canonical(length, char16_t(0xfffe));
                if (!(check & 2))
                    flags |= 0x40;
                if ((check & (1 | 16)) != 1)
                    flags |= 0x1000;
                if (iri && (check & (1 | 2 | 16 | 64 | 8)) == (2 | 8))
                    flags |= 0x40000000000ull;
            }
            s.offsets.end = static_cast<std::uint16_t>(position);
        }
    }
    s.flags |= flags | 0x80000000ull | 0x800000000ull;
}
}

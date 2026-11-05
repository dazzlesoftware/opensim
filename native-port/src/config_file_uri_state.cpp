// .NET8.0.31 Uri.CreateUriInfo/ParseRemaining file closure; MIT .NET license.
#include "opensim/framework/config_file_uri_state.hpp"
#include "opensim/framework/config_file_uri_canonical.hpp"
#include "opensim/framework/config_uri_iri.hpp"
namespace opensim::framework {
namespace {
using U = std::uint64_t;
constexpr U Implicit = 0x20000000, Authority = 0x100000, Dos = 0x8000000, Unc = 0x10000000,
            Unicode = 0x200000000, Rest = 0x800000000;
bool sep(char16_t c) {
    return c == u'/' || c == u'\\';
}
bool lws(char16_t c) {
    return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n';
}
void trim(std::u16string_view str, std::size_t& length, std::size_t index) {
    while (length > index && lws(str[length - 1]))
        --length;
}
}
void config_file_uri_create_info(ConfigFileUriState& s) {
    if (s.info)
        return;
    auto cf = s.flags;
    ConfigFileUriOffsets o;
    o.end = static_cast<std::uint16_t>(s.parse_string.size());
    bool flag = false;
    std::size_t i = 0;
    auto& text = s.parse_string;
    auto at = [&](std::size_t index) {
        if (index >= text.size())
            throw ConfigFileUriStateError(true);
        return text[index];
    };
    if (cf & Implicit) {
        while (lws(at(i))) {
            ++i;
            ++o.scheme;
        }
        if (cf & Unc) {
            i += 2;
            auto limit = cf & 65535;
            while (i < limit && sep(at(i)))
                ++i;
        }
    } else {
        i = 4;
        while (at(i++) != u':')
            ++o.scheme;
        if (cf & Authority) {
            if (at(i) == u'\\' || at(i + 1) == u'\\')
                flag = true;
            i += 2;
            if (cf & (Dos | Unc)) {
                auto limit = cf & 65535;
                while (i < limit && sep(at(i))) {
                    ++i;
                    flag = true;
                }
            }
        }
    }
    if ((cf & 0x70000) == 0x70000 || (cf & Dos)) {
        o.user = o.host = o.path = static_cast<std::uint16_t>(cf & 65535);
        cf &= ~U(65535);
        if (flag)
            cf |= 1;
    } else {
        o.user = static_cast<std::uint16_t>(i);
        if ((cf & 0x70000) == 0x50000) {
            o.host = static_cast<std::uint16_t>(i);
            o.path = static_cast<std::uint16_t>(cf & 65535);
            cf &= ~U(65535);
        } else {
            o.host = static_cast<std::uint16_t>(i);
            i = cf & 65535;
            cf &= ~U(65535);
            if (flag)
                cf |= 1;
            o.path = static_cast<std::uint16_t>(i);
            if (cf & Unicode)
                o.end = static_cast<std::uint16_t>(s.original_string.size());
        }
    }
    cf |= 0x40000000;
    s.offsets = o;
    s.info = true;
    s.flags = (s.flags & ~U(65535)) | cf;
}
void config_file_uri_parse_remaining(ConfigFileUriState& s) {
    if (s.flags & 0x80000000)
        return;
    config_file_uri_create_info(s);
    U flags = 0;
    bool normalize = (s.flags & (Unicode | Rest)) == Unicode;
    std::size_t position = s.offsets.scheme, length = s.parse_string.size();
    int check = 0;
    trim(s.parse_string, length, position);
    if (s.flags & Implicit)
        flags |= 1;
    else {
        std::u16string_view scheme = u"file";
        for (std::size_t i = 0; i < 4; ++i)
            if (scheme[i] != s.parse_string[position + i])
                flags |= 1;
        if ((s.flags & Authority) && (position + 7 >= length || s.parse_string[position + 5] != u'/' ||
                                      s.parse_string[position + 6] != u'/'))
            flags |= 1;
    }
    position = s.offsets.path;
    std::size_t source = s.offsets.path;
    if (normalize) {
        if (!(s.flags & Unc))
            s.parse_string = (s.flags & Implicit) ? u"" : u"file://";
        s.offsets.path = static_cast<std::uint16_t>(s.parse_string.size());
        position = s.offsets.path;
    }
    auto append_iri = [&](std::size_t start, std::size_t end, std::uint32_t component) {
        s.parse_string += config_uri_escape_unescape_iri(
            std::u16string_view(s.original_string).substr(start, end - start), component);
        if (s.parse_string.size() > 65535)
            throw ConfigFileUriStateError();
        length = s.parse_string.size();
    };
    if (normalize) {
        auto start = source;
        if (s.flags & Implicit)
            source = s.original_string.size();
        else {
            source = s.original_string.find_first_of(u"?#", source);
            if (source == s.original_string.npos)
                source = s.original_string.size();
        }
        append_iri(start, source, 16);
        if (s.parse_string == s.original_string)
            trim(s.parse_string, length, position);
    }
    auto canonical = [&](char16_t delimiter) {
        auto r = config_file_uri_check_canonical(s.parse_string, position, length, delimiter,
                                                 (s.flags & Implicit) != 0, (s.flags & Unicode) != 0);
        check = r.check;
        position = r.position;
    };
    canonical((s.flags & Implicit) ? char16_t(0xffff) : u'?');
    if ((s.flags & Authority) && (s.offsets.path == length || !sep(s.parse_string[s.offsets.path])))
        flags |= 0x4000;
    bool bad = false;
    bool convert = !s.unix_file_syntax;
    if ((s.flags & Dos) || (s.flags & Authority)) {
        if (check & 128) {
            flags |= 0x410;
            bad = true;
        }
        if (convert && (check & 16)) {
            flags |= 0x410;
            bad = true;
        }
        if ((flags & 0x400) || (check & 4))
            flags |= 0x2000;
        if (check & 16)
            flags |= 0x8000;
    } else if (check & 16) {
        flags |= 0x400;
        bad = true;
    }
    if (!(check & 2) && (!(s.flags & Implicit) || (s.flags & 0x80000) || (check & 32))) {
        flags |= 0x10;
        bad = true;
    }
    if ((s.flags & Implicit) && (check & (1 | 32)))
        check &= ~1;
    if (!(check & 1))
        flags |= 0x400;
    if (!bad && (check & (1 | 2 | 64 | 8)) == (2 | 8))
        flags |= 0x10000000000ull;
    if (normalize && source < s.original_string.size() && s.original_string[source] == u'?') {
        auto start = source++;
        source = s.original_string.find(u'#', source);
        if (source == s.original_string.npos)
            source = s.original_string.size();
        append_iri(start, source, 32);
        if (s.parse_string == s.original_string)
            trim(s.parse_string, length, position);
    }
    s.offsets.query = static_cast<std::uint16_t>(position);
    if (position < length && s.parse_string[position] == u'?') {
        ++position;
        canonical(u'#');
        if (!(check & 2))
            flags |= 0x20;
        if ((check & (1 | 16)) != 1)
            flags |= 0x800;
        if ((check & (1 | 2 | 16 | 64 | 8)) == (2 | 8))
            flags |= 0x20000000000ull;
    }
    if (normalize && source < s.original_string.size() && s.original_string[source] == u'#') {
        auto start = source;
        source = s.original_string.size();
        append_iri(start, source, 64);
        trim(s.parse_string, length, position);
    }
    s.offsets.fragment = static_cast<std::uint16_t>(position);
    if (position < length && s.parse_string[position] == u'#') {
        ++position;
        canonical(char16_t(0xfffe));
        if (!(check & 2))
            flags |= 0x40;
        if ((check & (1 | 16)) != 1)
            flags |= 0x1000;
        if ((check & (1 | 2 | 16 | 64 | 8)) == (2 | 8))
            flags |= 0x40000000000ull;
    }
    s.offsets.end = static_cast<std::uint16_t>(position);
    s.flags |= flags | 0x80000000ull | Rest;
}
}

// Native .NET8.0.31 built-in/V1 Uri constructor/getters; MIT .NET license.
#include "opensim/framework/config_file_uri_object.hpp"
#include "opensim/framework/config_uri_state.hpp"
#include "opensim/framework/config_uri_canonical.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_uri_network_identity.hpp"
#include "opensim/framework/config_uri_format.hpp"
#include "opensim/framework/config_file_uri_resources.hpp"
#include "opensim/framework/config_uri_relative.hpp"
#include "opensim/framework/config_uri_iri.hpp"
#include "opensim/framework/config_uri_unescape.hpp"
#include "opensim/framework/config_uri_escape.hpp"
#include "opensim/framework/text.hpp"
#include <array>
#include <charconv>
#include <sstream>
#include <vector>
#include <mutex>
namespace opensim::framework {
namespace {
using Text = ConfigUriText;
using V = std::u16string_view;
using U = std::uint64_t;
int hex(char16_t c) {
    return c >= u'0' && c <= u'9'   ? c - u'0'
           : c >= u'A' && c <= u'F' ? c - u'A' + 10
           : c >= u'a' && c <= u'f' ? c - u'a' + 10
                                    : -1;
}
bool digit(char16_t c) {
    return c >= u'0' && c <= u'9';
}
std::u16string number(std::uint32_t n) {
    auto x = std::to_string(n);
    return {x.begin(), x.end()};
}
Text text(std::u16string value) {
    return value.empty() ? runtime_empty_text() : std::make_shared<const std::u16string>(std::move(value));
}
[[noreturn]] void parsing_error(int value) {
    throw ConfigUriComponentsError("System.UriFormatException",
                                   std::u16string(config_file_uri_error_messages[value]), 0x80131537);
}
Text substring(const Text& source, std::int64_t start, std::int64_t count) {
    if (start > static_cast<std::int64_t>(source->size()))
        throw ConfigFileUriRangeError(
            u"startIndex cannot be larger than length of string. (Parameter 'startIndex')", u"startIndex");
    if (start < 0 || count < 0) {
        auto name = start < 0 ? u"startIndex" : u"length";
        auto value = start < 0 ? start : count;
        auto n = std::to_string(value);
        std::u16string num(n.begin(), n.end());
        std::u16string newline =
#ifdef _WIN32
            u"\r\n";
#else
            u"\n";
#endif
        throw ConfigFileUriRangeError(std::u16string(name) + u" ('" + num +
                                          u"') must be a non-negative value. (Parameter '" + name + u"')" +
                                          newline + u"Actual value was " + num + u".",
                                      name, value);
    }
    if (count > static_cast<std::int64_t>(source->size()) - start)
        throw ConfigFileUriRangeError(
            u"Index and length must refer to a location within the string. (Parameter 'length')", u"length");
    if (!count)
        return runtime_empty_text();
    if (start == 0 && count == static_cast<std::int64_t>(source->size()))
        return source;
    return text(source->substr(static_cast<std::size_t>(start), static_cast<std::size_t>(count)));
}
std::optional<std::u16string> ipv4(V s) {
    std::vector<std::uint64_t> values;
    std::size_t i = 0;
    while (i < s.size()) {
        int base = 10;
        std::uint64_t v = 0;
        bool had = false;
        if (s[i] == u'0') {
            base = 8;
            ++i;
            had = true;
            if (i < s.size() && (s[i] == u'x' || s[i] == u'X')) {
                base = 16;
                ++i;
                had = false;
            }
        }
        while (i < s.size() && s[i] != u'.') {
            int d = base == 16 ? hex(s[i]) : digit(s[i]) ? s[i] - u'0' : -1;
            if (d < 0 || d >= base)
                return {};
            v = v * base + d;
            if (v > 0xffffffff)
                return {};
            ++i;
            had = true;
        }
        if (!had)
            return {};
        values.push_back(v);
        if (i < s.size())
            ++i;
    }
    if (values.empty() || values.size() > 4 || s.back() == u'.')
        return {};
    for (std::size_t n = 0; n + 1 < values.size(); ++n)
        if (values[n] > 255)
            return {};
    std::uint64_t max = values.size() == 1   ? 0xffffffff
                        : values.size() == 2 ? 0xffffff
                        : values.size() == 3 ? 0xffff
                                             : 255;
    if (values.back() > max)
        return {};
    std::uint32_t v = static_cast<std::uint32_t>(values.back());
    for (std::size_t n = 0; n + 1 < values.size(); ++n)
        v |= static_cast<std::uint32_t>(values[n]) << (24 - 8 * n);
    return number(v >> 24) + u"." + number((v >> 16) & 255) + u"." + number((v >> 8) & 255) + u"." +
           number(v & 255);
}
std::pair<std::u16string, std::u16string> ipv6(V source) {
    V s = source.substr(1, source.size() - 2);
    std::u16string scope;
    auto zone = s.find(u'%');
    auto prefix = s.find(u'/');
    auto end = std::min(zone == V::npos ? s.size() : zone, prefix == V::npos ? s.size() : prefix);
    if (zone != V::npos) {
        auto zEnd = prefix == V::npos ? s.size() : prefix;
        scope = s.substr(zone, zEnd - zone);
    }
    s = s.substr(0, end);
    std::array<std::uint16_t, 8> nums{};
    std::vector<std::uint16_t> left, right;
    auto gap = s.find(u"::");
    auto read = [](V part, std::vector<std::uint16_t>& out) {
        std::size_t pos = 0;
        while (pos < part.size()) {
            auto stop = part.find(u':', pos);
            if (stop == V::npos)
                stop = part.size();
            auto value = part.substr(pos, stop - pos);
            if (value.find(u'.') != V::npos) {
                std::uint32_t v = 0;
                std::size_t n = 0;
                for (int k = 0; k < 4; ++k) {
                    int b = 0;
                    while (n < value.size() && value[n] != u'.')
                        b = b * 10 + value[n++] - u'0';
                    ++n;
                    v = (v << 8) | b;
                }
                out.push_back(v >> 16);
                out.push_back(v & 65535);
            } else {
                int v = 0;
                for (auto c : value)
                    v = v * 16 + hex(c);
                out.push_back(v);
            }
            pos = stop + 1;
        }
    };
    read(gap == V::npos ? s : s.substr(0, gap), left);
    if (gap != V::npos)
        read(s.substr(gap + 2), right);
    for (std::size_t i = 0; i < left.size() && i < 8; ++i)
        nums[i] = left[i];
    for (std::size_t i = 0; i < right.size() && i < 8; ++i)
        nums[8 - right.size() + i] = right[i];
    int best = -1, len = 0, run = 0;
    for (int i = 0; i < 8; ++i) {
        if (!nums[i]) {
            ++run;
            if (run > len) {
                len = run;
                best = i - run + 1;
            }
        } else
            run = 0;
    }
    if (len <= 1) {
        best = -1;
        len = 0;
    }
    bool v4 =
        (nums[0] == 0 && nums[1] == 0 && nums[2] == 0 && nums[3] == 0 && nums[6] != 0 &&
         ((nums[4] == 0 && (nums[5] == 0 || nums[5] == 65535)) || (nums[4] == 65535 && nums[5] == 0))) ||
        (nums[4] == 0 && nums[5] == 24318);
    std::u16string out = u"[";
    for (int i = 0; i < 8; ++i) {
        if (v4 && i == 6) {
            out += u':';
            out += number(nums[i] >> 8) + u"." + number(nums[i] & 255) + u"." + number(nums[i + 1] >> 8) +
                   u"." + number(nums[i + 1] & 255);
            break;
        }
        if (i == best) {
            out += u':';
            if (best + len == 8) {
                out += u':';
                break;
            }
        }
        if (best > i || i >= best + len) {
            if (i)
                out += u':';
            char digits[4];
            auto converted = std::to_chars(digits, digits + 4, nums[i], 16);
            out.append(digits, converted.ptr);
        }
    }
    out += u']';
    return {out, scope};
}

class GenericUriObject final : public ConfigUriObject {
    mutable ConfigUriState state_;
    Text original_;
    std::shared_ptr<ConfigUriComponentsBackend> backend_;
    bool windows_;
    mutable std::recursive_mutex mutex_;
    mutable Text parse_, host_, absolute_, display_, fragment_, idn_, path_, query_;
    mutable std::optional<std::u16string> scope_;
    bool has(ConfigUriSyntaxFlags f) const {
        return (state_.syntax->flags & static_cast<U>(f)) != 0;
    }
    void remaining() const {
        if (!(state_.flags & 0x80000000ull)) {
            bool normalize = (state_.flags & 0xa00000000ull) == 0x200000000ull;
            try {
                config_uri_parse_remaining(state_);
            } catch (const ConfigUriStateError& e) {
                throw ConfigUriComponentsError(e.source_type, utf16_replacing(e.what()), e.hresult);
            }
            if (normalize)
                parse_ = text(state_.parse_string);
        }
    }
    void host() const {
        if (host_)
            return;
        remaining();
        auto& s = state_;
        auto o = s.offsets;
        auto type = s.flags & 0x70000ull;
        auto raw = (type == 0x50000 && (s.flags & 0x8000000ull)) || type == 0x70000
                       ? std::u16string{}
                       : *substring(parse_, o.host, std::int64_t(o.path) - o.host);
        std::u16string value;
        bool loopback = false;
        if (type == 0x30000 || type == 0x40000) {
            auto colon = raw.rfind(u':');
            if (colon != raw.npos)
                raw.resize(colon);
            auto upper = raw.find_first_of(u"ABCDEFGHIJKLMNOPQRSTUVWXYZ");
            if (upper != raw.npos)
                value = *backend_->lower_invariant(text(raw));
            else if (raw == u"localhost" || raw == u"loopback") {
                loopback = true;
                value = u"localhost";
            } else
                value = raw;
        } else if (type == 0x20000) {
            auto colon = raw.find(u':');
            if (colon != raw.npos)
                raw.resize(colon);
            auto ip = ipv4(raw);
            if (!ip)
                parsing_error(8);
            value = *ip;
            loopback = value.starts_with(u"127.");
        } else if (type == 0x10000) {
            auto close = raw.find(u']');
            if (close == raw.npos)
                parsing_error(8);
            raw.resize(close + 1);
            auto ip = ipv6(raw);
            value = std::move(ip.first);
            if (!ip.second.empty())
                scope_ = std::move(ip.second);
            loopback = value == u"[::1]" || value == u"[::127.0.0.1]" || value == u"[::ffff:127.0.0.1]";
        } else if (type == 0x50000) {
            value = (s.flags & 0x8000000ull) ? u"" : raw;
            loopback = value.empty();
        } else if (type != 0x70000)
            parsing_error(8);
        if (loopback)
            s.flags |= 0x400000ull;
        if (!value.empty()) {
            if (type == 0x50000) {
                auto c = config_uri_check_canonical(value, 0, value.size(), char16_t(0xffff),
                                                    (s.flags & 0x20000000ull) != 0,
                                                    (s.flags & 0x200000000ull) != 0, s.syntax.get())
                             .check;
                if (!(c & 2) && (!(s.flags & 0x20000000ull) || (c & 32)))
                    s.flags |= 4;
                if ((s.flags & 0x20000000ull) && (c & (1 | 32)))
                    c &= ~1;
                if ((c & (1 | 16)) != 1) {
                    s.flags |= 0x100;
                    if (!(s.flags & 0x80000ull))
                        value = config_uri_escape(value, !(s.flags & 0x20000000ull),
                                                  ConfigUriEscapeSet::ExceptQuestionMarkHash);
                }
            } else if (!(s.flags & 0x2000000ull)) {
                if (scope_)
                    s.flags |= 0x104;
                else
                    for (std::size_t i = 0; i < value.size(); ++i)
                        if (o.host + i >= o.end || value[i] != s.parse_string[o.host + i]) {
                            s.flags |= 0x104;
                            break;
                        }
            }
        }
        host_ = text(std::move(value));
    }
    unsigned mask(int format) const {
        auto f = state_.flags;
        unsigned result = format == 1 ? unsigned((f & 0x3f80) >> 6) : unsigned(f & 127);
        if (format == 1 && (f & 1))
            result |= 1;
        if (f & (0x2000 | 0x4000 | 0x8000))
            result |= 16;
        else if (f & 0x8000000) {
            auto p = state_.offsets.path;
            auto secured = (state_.parse_string[p] == u'/' || state_.parse_string[p] == u'\\') ? 3 : 2;
            if (state_.parse_string[p + secured - 1] == u'|')
                result |= 16;
        }
        return result;
    }
    std::u16string component(bool fragment, int format, unsigned nc) const {
        auto o = state_.offsets;
        int start = fragment ? o.fragment : o.query, end = fragment ? o.end : o.fragment;
        if (start == end)
            return {};
        auto raw = *substring(parse_, start + 1, end - start - 1);
        auto bit = fragment ? 64u : 32u;
        int mode = 0;
        if (nc & bit) {
            if (format == 1 && !(state_.flags & 0x80000))
                return config_uri_escape(raw, true,
                                         fragment ? ConfigUriEscapeSet::UnreservedReserved
                                                  : ConfigUriEscapeSet::ExceptHash);
            if (format != 1)
                mode = format == 32767 ? ((state_.flags & 0x80000) ? 6 : 7)
                       : format == 2   ? 10
                                       : ((state_.flags & 0x80000) ? 2 : 3);
        }
        return config_uri_unescape(raw, u'#', 0xffff, 0xffff, mode,
                                   has(ConfigUriSyntaxFlags::AllowIriParsing), !fragment);
    }
    Text parts(int format, unsigned bits, bool keep = true) const {
        remaining();
        if (bits == 1)
            return state_.syntax->scheme;
        if (bits & 4)
            host();
        auto nc = mask(format);
        auto o = state_.offsets;
        if (!(nc & bits)) {
            if (bits == 2) {
                if (!(state_.flags & 0x200000ull))
                    return runtime_empty_text();
                auto end = std::int64_t(o.host) - (keep ? 0 : 1);
                if (o.user >= end)
                    return runtime_empty_text();
                return substring(parse_, o.user, end - o.user);
            }
            if (bits == 127)
                return substring(parse_, o.scheme, std::int64_t(o.end) - o.scheme);
            if (bits == 16) {
                auto start = o.path;
                if (!keep && (state_.flags & 0x100000) && start < o.query &&
                    state_.parse_string[start] == u'/')
                    ++start;
                return substring(parse_, start, std::int64_t(o.query) - start);
            }
            if (bits == 64 || bits == 32) {
                int start = bits == 64 ? o.fragment : o.query, end = bits == 64 ? o.end : o.fragment;
                if (start == end)
                    return runtime_empty_text();
                if (!keep)
                    ++start;
                return substring(parse_, start, end - start);
            }
        }
        host();
        std::u16string dest;
        if (bits & 1) {
            dest += *state_.syntax->scheme;
            if (bits != 1) {
                dest += u':';
                if (state_.flags & 0x100000)
                    dest += u"//";
            }
        }
        if ((bits & 2) && (state_.flags & 0x200000)) {
            auto raw = *substring(parse_, o.user, std::int64_t(o.host) - o.user);
            if (nc & 2) {
                if (format == 1) {
                    dest += (state_.flags & 0x80000)
                                ? raw
                                : config_uri_escape(raw, true, ConfigUriEscapeSet::ExceptQuestionMarkHash);
                } else if (format == 3) {
                    dest += config_uri_unescape(V(raw).substr(0, raw.size() - 1), u'@', u'/', u'\\',
                                                (state_.flags & 0x80000) ? 2 : 3,
                                                has(ConfigUriSyntaxFlags::AllowIriParsing), false);
                    dest += u'@';
                } else if (format == 2)
                    dest += config_uri_unescape(raw, 0xffff, 0xffff, 0xffff, 10,
                                                has(ConfigUriSyntaxFlags::AllowIriParsing), false);
                else
                    dest += raw;
            } else
                dest += raw;
            if (bits == 2 && !keep && !dest.empty())
                dest.pop_back();
        }
        if (bits & 4) {
            int mode = (format != 1 && (state_.flags & 0x70000) == 0x50000 && (nc & 4))
                           ? (format == 2 ? 10 : ((state_.flags & 0x80000) ? 2 : 3))
                           : 0;
            dest += config_uri_unescape(*host_, u'/', u'?', u'#', mode,
                                        has(ConfigUriSyntaxFlags::AllowIriParsing), false);
        }
        if ((bits & 8) && (state_.flags & 0x800000))
            dest += u":" + number(o.port_value);
        if (bits & 16) {
            dest = config_uri_canonical_path(state_, std::move(dest), format, windows_);
            if (bits == 16 && !keep && (state_.flags & 0x100000) && !dest.empty() && dest.front() == u'/')
                dest.erase(dest.begin());
        }
        if ((bits & 32) && o.query < o.fragment) {
            if (bits != 32 || keep)
                dest += u'?';
            dest += component(false, format, nc);
        }
        if ((bits & 64) && o.fragment < o.end) {
            if (bits != 64 || keep)
                dest += u'#';
            dest += component(true, format, nc);
        }
        return text(std::move(dest));
    }

  public:
    GenericUriObject(Text original, ConfigUriState state, std::shared_ptr<ConfigUriComponentsBackend> backend,
                     bool windows)
        : state_(std::move(state)), original_(std::move(original)), backend_(std::move(backend)),
          windows_(windows) {
        parse_ = (state_.flags & 0x200000000ull) ? text(state_.parse_string) : original_;
    }

    ConfigUriObjectPtr clone_source() const {
        std::lock_guard lock(mutex_);
        auto state = state_;
        if (state.flags & 0x40000000ull) {
            state.flags &= ~(0xffffull | 0x40000000ull | 0x80000000ull);
            std::size_t cursor = state.offsets.path;
            if (state.flags & 0x800000ull) {
                auto at = [&](std::size_t pos) {
                    if (pos >= state.parse_string.size())
                        throw ConfigUriComponentsError("System.IndexOutOfRangeException",
                                                       u"Index was outside the bounds of the array.",
                                                       0x80131508);
                    return state.parse_string[pos];
                };
                while (at(cursor) != u':' && cursor > state.offsets.host)
                    --cursor;
                if (at(cursor) != u':')
                    cursor = state.offsets.path;
            }
            state.flags |= cursor;
        }
        state.info = false;
        auto result = std::make_shared<GenericUriObject>(original_, std::move(state), backend_, windows_);
        result->parse_ = parse_;
        return result;
    }
    ConfigUriObjectPtr
    resolve(Text input, const std::function<ConfigUriObjectPtr(Text, ConfigUriObjectKind)>& create) const {
        std::lock_guard lock(mutex_);
        auto lws = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; };
        auto slash = [](char16_t c) { return c == u'/' || c == u'\\'; };
        auto alpha = [](char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); };
        std::size_t start = 0, end = input->size();
        while (start < end && lws((*input)[start]))
            ++start;
        while (end > start && lws((*input)[end - 1]))
            --end;
        auto value = substring(input, start, end - start);
        auto flags = state_.flags;
        bool implicit = (flags & 0x20000000ull) != 0, dos = (flags & 0x8000000ull) != 0,
             unc = (flags & 0x10000000ull) != 0;
        auto make = [&](std::u16string v) {
            return create(text(std::move(v)), ConfigUriObjectKind::Absolute);
        };
        if (value->empty())
            return create(parts((flags & 0x80000) ? 1 : 3, 127), ConfigUriObjectKind::Absolute);
        if (value->front() == u'#' && !implicit && has(ConfigUriSyntaxFlags::MayHaveFragment))
            return make(*parts(1, 63) + *value);
        if (value->front() == u'?' && !implicit && has(ConfigUriSyntaxFlags::MayHaveQuery))
            return make(*parts(1, 31) + *value);
        if (value->size() >= 3 && ((*value)[1] == u':' || (*value)[1] == u'|') && alpha((*value)[0]) &&
            slash((*value)[2])) {
            if (implicit)
                return create(value, ConfigUriObjectKind::Absolute);
            if (has(ConfigUriSyntaxFlags::AllowDOSPath))
                return make(*scheme() +
                            ((flags & 0x100000) ? (has(ConfigUriSyntaxFlags::PathIsRooted) ? u":///" : u"://")
                                                : (has(ConfigUriSyntaxFlags::PathIsRooted) ? u":/" : u":")) +
                            *value);
        }
        for (std::size_t i = 0;
             i < value->size() && !slash((*value)[i]) && (*value)[i] != u'?' && (*value)[i] != u'#'; ++i)
            if ((*value)[i] == u':') {
                if (i < 2)
                    break;
                bool valid = alpha((*value)[0]);
                for (std::size_t n = 1; n < i; ++n) {
                    auto c = (*value)[n];
                    valid = valid && (alpha(c) || digit(c) || c == u'+' || c == u'-' || c == u'.');
                }
                if (!valid || i > 1024)
                    break;
                auto name = value->substr(0, i);
                for (auto& c : name)
                    if (c >= u'A' && c <= u'Z')
                        c += 32;
                auto syntax = native_config_uri_syntax_catalog().find_or_fetch(text(std::move(name)));
                if (syntax == state_.syntax)
                    value = substring(value, i + 1, value->size() - i - 1);
                else
                    return create(value, ConfigUriObjectKind::Absolute);
                break;
            }
        if (value->empty()) {
            if (original_ == parse_)
                return clone_source();
            return create(original_, ConfigUriObjectKind::Absolute);
        }
        auto c = value->front();
        if (dos && slash(c) && (value->size() == 1 || !slash((*value)[1]))) {
            auto at = original_->find(u':');
            if (!implicit)
                at = original_->find(u':', at == original_->npos ? 0 : at + 1);
            return make((at == original_->npos ? std::u16string{} : original_->substr(0, at + 1)) + *value);
        }
        if (has(ConfigUriSyntaxFlags::FileLikeUri) && slash(c)) {
            if (value->size() >= 2 && slash((*value)[1]))
                return implicit ? create(value, ConfigUriObjectKind::Absolute) : make(u"file:" + *value);
            if (unc) {
                auto path = *parts(2, 16);
                auto stop = path.empty() ? path.npos : path.find(u'/', 1);
                if (stop != path.npos)
                    path.resize(stop);
                return make((implicit ? u"\\\\" : u"file://") + *parts(implicit ? 2 : 3, 4) + path + *value);
            }
            return make(u"file://" + *value);
        }
        auto prefix = [&] {
            host();
            if ((state_.flags & 0x70000ull) != 0x10000)
                return *parts(3, 15);
            auto dns = host_->substr(1, host_->size() - 2);
            if (scope_)
                dns += *scope_;
            return *parts(3, 3) + u"[" + dns + u"]" + *parts(3, 8);
        };
        bool convert = has(ConfigUriSyntaxFlags::ConvertPathSlashes);
        if (c == u'/' || (c == u'\\' && convert)) {
            if (value->size() >= 2 && (*value)[1] == u'/')
                return make(*scheme() + u":" + *value);
            return make(prefix() + (c == u'\\' && convert ? u"/" + value->substr(1) : *value));
        }
        auto path = *parts(implicit ? 2 : 3, 16);
        auto directory = path.rfind(u'/');
        path.resize(directory == path.npos ? 0 : directory + 1);
        auto stop = value->size();
        if (has(ConfigUriSyntaxFlags::MayHaveQuery))
            stop = std::min(stop, value->find(u'?'));
        if (!implicit && has(ConfigUriSyntaxFlags::MayHaveFragment))
            stop = std::min(stop, value->find(u'#'));
        auto body = path + value->substr(0, stop), suffix = value->substr(stop);
        std::size_t secured = dos ? ((state_.parse_string[state_.offsets.path] == u'/' ||
                                      state_.parse_string[state_.offsets.path] == u'\\')
                                         ? 3
                                         : 2)
                                  : 0;
        auto compress = [&](std::size_t begin) {
            auto tail = config_file_uri_compress(body.substr(begin), convert,
                                                 has(ConfigUriSyntaxFlags::CanonicalizeAsFilePath));
            body.resize(begin);
            body += tail;
        };
        if (implicit && dos) {
            compress(3);
            return make(body.substr(1) + suffix);
        }
        std::u16string head;
        if (implicit) {
            host();
            if ((state_.flags & 0x70000ull) == 0x10000) {
                auto dns = host_->substr(1, host_->size() - 2);
                if (scope_)
                    dns += *scope_;
                head = u"\\\\[" + dns + u"]";
            } else {
                if (windows_ || !(flags & 0x100000000000ull))
                    head = u"\\\\";
                head += *parts(2, 4);
            }
        } else
            head = prefix();
        compress(secured);
        return make(head + body + suffix);
    }

    Text builder_text(Text replacement) const {
        std::lock_guard lock(mutex_);
        auto name = scheme();
        auto hostname = parts(1, 4);
        auto port =
            (state_.flags & 0x800000ull) ? int(state_.offsets.port_value) : state_.syntax->default_port;
        auto path = *parts(1, 16);
        if ((state_.flags & 0x8000000ull) && !path.empty() && path.front() == u'/')
            path.erase(path.begin());
        auto query = parts(1, 32);
        fragment();
        auto info = *parts(1, 2, false);
        auto colon = info.find(u':');
        auto user = info.substr(0, colon),
             password = colon == info.npos ? std::u16string{} : info.substr(colon + 1);
        if (user.empty() && !password.empty())
            throw ConfigUriComponentsError("System.UriFormatException",
                                           u"Invalid URI: The username:password construct is badly formed.",
                                           0x80131537);
        auto syntax = native_config_uri_syntax_catalog().get(*name);
        bool authority = syntax ? ((syntax->flags & std::uint64_t(ConfigUriSyntaxFlags::MustHaveAuthority)) ||
                                   (!hostname->empty() &&
                                    !(syntax->flags & std::uint64_t(ConfigUriSyntaxFlags::MailToLikeUri)) &&
                                    (syntax->flags & std::uint64_t(ConfigUriSyntaxFlags::OptionalAuthority))))
                                : !hostname->empty();
        std::u16string value = *name + (authority ? u"://" : u":");
        auto encode = [](V input) {
            std::u16string result;
            for (auto c : input) {
                switch (c) {
                case u'/':
                    result += u"%2F";
                    break;
                case u'\\':
                    result += u"%5C";
                    break;
                case u'?':
                    result += u"%3F";
                    break;
                case u'#':
                    result += u"%23";
                    break;
                case u'@':
                    result += u"%40";
                    break;
                default:
                    result += c;
                }
            }
            return result;
        };
        if (!user.empty()) {
            value += encode(user);
            if (!password.empty())
                value += u":" + encode(password);
            value += u'@';
        }
        if (!hostname->empty()) {
            value += *hostname;
            if (port != -1)
                value += u":" + number(port);
        }
        if (!path.empty()) {
            if (path.front() != u'/' && !hostname->empty())
                value += u'/';
            value += path;
        }
        value += *query;
        if (replacement && !replacement->empty()) {
            if (replacement->front() != u'#')
                value += u'#';
            value += *replacement;
        }
        return text(std::move(value));
    }
    void builder_fields() const {
        std::lock_guard lock(mutex_);
        scheme();
        parts(1, 4);
        auto path = parts(1, 16);
        if ((state_.flags & 0x8000000ull) && !path->empty() && path->front() == u'/')
            substring(path, 1, path->size() - 1);
        parts(1, 32);
        fragment();
        parts(1, 2, false);
    }

    Text network_component(ConfigGenericUriComponent component) const {
        std::lock_guard lock(mutex_);
        using C = ConfigGenericUriComponent;
        if (component == C::Host) {
            host();
            return host_;
        }
        if (component == C::IdnHost) {
            if (!idn_) {
                host();
                auto type = state_.flags & 0x70000ull;
                auto value = host_;
                if (type == 0x30000ull) {
                    if (!backend_->idn_ascii)
                        throw std::invalid_argument("URI IdnHost requires exact IDNA provider");
                    value = backend_->idn_ascii(value);
                } else if (type == 0x10000ull) {
                    auto raw = host_->substr(1, host_->size() - 2);
                    if (scope_)
                        raw += *scope_;
                    value = text(std::move(raw));
                } else if (type == 0x50000ull && (state_.flags & 0x104ull))
                    value = text(config_uri_unescape(*host_, 0xffff, 0xffff, 0xffff, 10,
                                                     has(ConfigUriSyntaxFlags::AllowIriParsing), false));
                idn_ = std::move(value);
            }
            return idn_;
        }
        if (component == C::DnsSafeHost) {
            host();
            auto type = state_.flags & 0x70000ull;
            return type == 0x10000ull || (type == 0x50000ull && (state_.flags & 0x104ull))
                       ? network_component(C::IdnHost)
                       : host_;
        }
        if (component == C::UserInfo)
            return parts(1, 2, false);
        if (component == C::AbsolutePath) {
            if (!path_)
                path_ = parts(1, 16);
            if ((state_.flags & 0x8000000ull) && !path_->empty() && path_->front() == u'/')
                return substring(path_, 1, path_->size() - 1);
            return path_;
        }
        if (component == C::Query) {
            if (!query_)
                query_ = parts(1, 32);
            return query_;
        }
        throw std::invalid_argument("Invalid native URI component selector");
    }
    ConfigUriNetworkIdentity network_identity() const {
        std::lock_guard lock(mutex_);
        host();
        auto type = state_.flags & 0x70000ull;
        int value = type == 0x10000                        ? 4
                    : type == 0x20000                      ? 3
                    : type == 0x30000                      ? 2
                    : (type == 0x40000 || type == 0x50000) ? 1
                                                           : 0;
        return {value, (state_.flags & 0x400000ull) != 0};
    }
    std::int32_t port_source() const {
        std::lock_guard lock(mutex_);
        try {
            config_uri_create_info(state_);
        } catch (const ConfigUriStateError& e) {
            throw ConfigUriComponentsError(e.source_type, utf16_replacing(e.what()), e.hresult);
        }
        return (state_.flags & 0x800000ull) ? state_.offsets.port_value : state_.syntax->default_port;
    }
    Text authority() const {
        std::lock_guard lock(mutex_);
        const auto value = network_component(ConfigGenericUriComponent::Host);
        if (!(state_.flags & 0x800000ull))
            return value;
        auto authority = *value + u':' + utf16_replacing(std::to_string(state_.offsets.port_value));
        return std::make_shared<const std::u16string>(std::move(authority));
    }
    Text path_parts(int format) const {
        std::lock_guard lock(mutex_);
        return parts(format, 16);
    }
    Text http_pool_identity() const {
        std::lock_guard lock(mutex_);
        const auto& protocol = *state_.syntax->scheme;
        if (protocol != u"http" && protocol != u"https" &&
            protocol != u"socks4" && protocol != u"socks4a" && protocol != u"socks5")
            throw std::invalid_argument("HTTP proxy pool identity requires an HTTP, HTTPS, or SOCKS proxy URI");
        return parts(3, 61);
    }
    bool is_absolute() const override {
        return true;
    }
    Text original_string() const override {
        return original_;
    }
    Text scheme() const override {
        return state_.syntax->scheme;
    }
    Text absolute_uri() const override {
        std::lock_guard lock(mutex_);
        if (!absolute_)
            absolute_ = parts(1, 127);
        return absolute_;
    }
    Text to_string() const override {
        std::lock_guard lock(mutex_);
        if (!display_)
            display_ = parts(32767, 127);
        return display_;
    }
    Text fragment() const override {
        std::lock_guard lock(mutex_);
        if (!fragment_)
            fragment_ = parts(1, 64);
        return fragment_;
    }
    Text local_path() const override {
        std::lock_guard lock(mutex_);
        remaining();
        auto& s = state_;
        auto o = s.offsets;
        bool dos = (s.flags & 0x8000000) != 0, unc = (s.flags & 0x10000000) != 0,
             implicit = (s.flags & 0x20000000) != 0;
        if (dos || unc) {
            host();
            if (!(s.flags & (4 | 16 | 0x2000))) {
                int start = unc ? int(o.host) - 2 : o.path;
                Text value;
                if (implicit && o.host == (dos ? 0 : 2) && o.query == o.end)
                    value = parse_;
                else {
                    if (dos && (s.parse_string[start] == u'/' || s.parse_string[start] == u'\\'))
                        ++start;
                    value = substring(parse_, start, int(o.query) - start);
                }
                if (dos && value->size() > 1 && (*value)[1] == u'|') {
                    auto changed = *value;
                    changed[1] = u':';
                    value = text(std::move(changed));
                }
                if (value->find(u'/') != value->npos) {
                    auto changed = *value;
                    for (auto& c : changed)
                        if (c == u'/')
                            c = u'\\';
                    value = text(std::move(changed));
                }
                return value;
            }
            int start = o.path;
            std::u16string dest;
            if (unc)
                dest = u"\\\\" + *host_;
            else if (s.parse_string[start] == u'/' || s.parse_string[start] == u'\\')
                ++start;
            auto secured = dest.size();
            int mode = (s.flags & 16) && !implicit ? 10 : 0;
            dest += config_uri_unescape(*substring(parse_, start, int(o.query) - start), 0xffff, 0xffff,
                                        0xffff, mode, has(ConfigUriSyntaxFlags::AllowIriParsing), true);
            if (dest.size() > 1 && dest[1] == u'|')
                dest[1] = u':';
            if (s.flags & 0x2000) {
                auto prefix = secured + (dos ? 2 : 0);
                auto compressed = config_file_uri_compress(dest.substr(prefix),
                                                           has(ConfigUriSyntaxFlags::ConvertPathSlashes),
                                                           has(ConfigUriSyntaxFlags::CanonicalizeAsFilePath));
                dest.resize(prefix);
                dest += compressed;
            }
            for (auto& c : dest)
                if (c == u'/')
                    c = u'\\';
            return text(std::move(dest));
        }
        return parts(2, 16);
    }
};
}
ConfigUriObjectPtr config_generic_uri_create(ConfigUriText input, ConfigUriObjectKind kind,
                                             std::shared_ptr<ConfigUriComponentsBackend> backend,
                                             bool windows, bool user_escaped) {
    if (!input)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'uriString')", 0x80004003,
                                       u"uriString");
    auto k = static_cast<std::int32_t>(kind);
    if (k < 0 || k > 2) {
        auto n = std::to_string(k);
        throw ConfigUriComponentsError("System.ArgumentException",
                                       u"The value '" + std::u16string(n.begin(), n.end()) +
                                           u"' passed for the UriKind parameter is invalid.",
                                       0x80070057);
    }
    if (!backend || !backend->unicode.normalize_nfc || !backend->lower_invariant)
        throw std::invalid_argument("Generic URI requires exact Unicode and lowercase providers");
    auto scan = config_file_uri_scan_scheme(*input, windows);
    if (scan.error) {
        if (scan.error > 4 || k == 1)
            parsing_error(scan.error);
        auto parse =
            config_uri_has_iri_characters(*input) ? text(config_uri_escape_unescape_iri(*input, 0)) : input;
        return config_uri_relative_object(input, parse);
    }
    if (config_file_uri_implicit_kind_is_relative(scan, *input, k, windows))
        return config_uri_relative_object(input, input);
    auto& catalog = native_config_uri_syntax_catalog();
    auto syntax = scan.unix_file_syntax ? catalog.unix_file() : catalog.find_or_fetch(text(scan.scheme));
    auto flags = scan.flags | (user_escaped ? 0x80000ull : 0ull);
    bool iri = (syntax->flags & std::uint64_t(ConfigUriSyntaxFlags::AllowIriParsing)) &&
               config_uri_has_iri_characters(*input);
    if (iri)
        flags |= 0x200000000ull;
    auto parsed = config_uri_parse_minimal(*input, *input, flags, syntax, windows, backend->unicode);
    int error = parsed.error;
    if (error && k != 1 && error <= 4)
        return config_uri_relative_object(input, input);
    if (!error && k == 2)
        error = 12;
    ConfigUriState state;
    state.flags = parsed.flags;
    state.parse_string = std::move(parsed.parse_string);
    state.original_string = *input;
    state.syntax = std::move(parsed.syntax);
    if (iri)
        try {
            config_uri_parse_remaining(state);
        } catch (const ConfigUriStateError& e) {
            throw ConfigUriComponentsError(e.source_type, utf16_replacing(e.what()), e.hresult);
        }
    if (error)
        parsing_error(error);
    return std::make_shared<GenericUriObject>(input, std::move(state), std::move(backend), windows);
}
ConfigUriObjectPtr config_generic_uri_try_create(ConfigUriText input, ConfigUriObjectKind kind,
                                                 std::shared_ptr<ConfigUriComponentsBackend> backend,
                                                 bool windows) {
    if (!input)
        return {};
    auto k = static_cast<std::int32_t>(kind);
    if (k >= 0 && k <= 2 && k != 1) {
        auto scan = config_file_uri_scan_scheme(*input, windows);
        if (scan.error > 0 && scan.error <= 4)
            return config_uri_relative_object(input, input);
    }
    try {
        return config_generic_uri_create(std::move(input), kind, std::move(backend), windows);
    } catch (const ConfigUriComponentsError& e) {
        if (e.source_type == "System.UriFormatException")
            return {};
        throw;
    }
}
ConfigUriText config_generic_uri_path(ConfigUriObjectPtr uri, std::int32_t format) {
    if (format != 1 && format != 2 && format != 3 && format != 32767)
        throw ConfigUriComponentsError(
            "System.ArgumentOutOfRangeException",
            u"Specified argument was out of the range of valid values. (Parameter 'uriFormat')", 0x80131502,
            u"uriFormat");
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("Generic URI path requires its native absolute object");
    return native->path_parts(format);
}

ConfigUriObjectPtr config_generic_uri_combine_string(
    ConfigUriObjectPtr base, ConfigUriText relative,
    const std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)>& create) {
    if (!base)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'baseUri')", 0x80004003,
                                       u"baseUri");
    if (!base->is_absolute())
        throw ConfigUriComponentsError(
            "System.ArgumentOutOfRangeException",
            u"Specified argument was out of the range of valid values. (Parameter 'baseUri')", 0x80131502,
            u"baseUri");
    if (!create)
        throw std::invalid_argument("Generic URI resolution requires global constructor dispatch");
    auto parsed = create(relative ? relative : runtime_empty_text(), ConfigUriObjectKind::RelativeOrAbsolute);
    if (parsed->is_absolute())
        return parsed;
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(base);
    if (!native)
        throw std::invalid_argument("Generic URI resolution requires its native base object");
    return native->resolve(parsed->original_string(), create);
}
bool config_generic_uri_owns(ConfigUriObjectPtr uri) {
    return bool(std::dynamic_pointer_cast<const GenericUriObject>(uri));
}

ConfigUriObjectPtr config_generic_uri_combine_uri(
    ConfigUriObjectPtr base, ConfigUriObjectPtr relative,
    const std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)>& create) {
    if (!base)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'baseUri')", 0x80004003,
                                       u"baseUri");
    if (!base->is_absolute())
        throw ConfigUriComponentsError(
            "System.ArgumentOutOfRangeException",
            u"Specified argument was out of the range of valid values. (Parameter 'baseUri')", 0x80131502,
            u"baseUri");
    if (!relative)
        throw ConfigUriComponentsError("System.NullReferenceException",
                                       u"Object reference not set to an instance of an object.", 0x80004003);
    if (relative->is_absolute()) {
        auto native = std::dynamic_pointer_cast<const GenericUriObject>(relative);
        if (!native)
            throw std::invalid_argument("Generic URI clone requires its native relative object");
        return native->clone_source();
    }
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(base);
    if (!native)
        throw std::invalid_argument("Generic URI resolution requires its native base object");
    if (!create)
        throw std::invalid_argument("Generic URI resolution requires global constructor dispatch");
    return native->resolve(relative->original_string(), create);
}
ConfigUriObjectPtr config_generic_uri_with_fragment(
    ConfigUriObjectPtr uri, ConfigUriText replacement,
    const std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)>& create) {
    if (!uri)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'uri')", 0x80004003, u"uri");
    uri->scheme();
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("Generic URI builder requires its native object");
    if (!create)
        throw std::invalid_argument("Generic URI builder requires global constructor dispatch");
    auto result = create(native->builder_text(replacement), ConfigUriObjectKind::Absolute);
    auto rebuilt = std::dynamic_pointer_cast<const GenericUriObject>(result);
    if (!rebuilt)
        throw std::invalid_argument("Generic URI builder field validation requires its native result object");
    rebuilt->builder_fields();
    return result;
}
ConfigUriObjectPtr config_generic_uri_clone(ConfigUriObjectPtr uri) {
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("Generic URI clone requires its native object");
    return native->clone_source();
}
ConfigUriText config_generic_uri_component(ConfigUriObjectPtr uri, ConfigGenericUriComponent component) {
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("Generic URI components require their native absolute object");
    return native->network_component(component);
}
std::int32_t config_generic_uri_port(ConfigUriObjectPtr uri) {
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("Generic URI port requires its native absolute object");
    return native->port_source();
}
ConfigUriText config_generic_uri_authority(ConfigUriObjectPtr uri) {
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("Generic URI authority requires its native absolute object");
    return native->authority();
}
ConfigUriText config_generic_uri_http_pool_identity(ConfigUriObjectPtr uri) {
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("HTTP proxy pool identity requires its native absolute object");
    return native->http_pool_identity();
}
ConfigUriNetworkIdentity config_generic_uri_network_identity(ConfigUriObjectPtr uri) {
    auto native = std::dynamic_pointer_cast<const GenericUriObject>(uri);
    if (!native)
        throw std::invalid_argument("Generic URI identity requires its native absolute object");
    return native->network_identity();
}

}

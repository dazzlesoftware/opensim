// Source .NET8.0.31 Uri constructor/getters built-in file closure; MIT .NET license.
#include "opensim/framework/config_file_uri_object.hpp"
#include "opensim/framework/config_file_uri_state.hpp"
#include "opensim/framework/config_file_uri_format.hpp"
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
class FileUriObject final : public ConfigUriObject {
    mutable ConfigFileUriState state_;
    Text original_;
    std::shared_ptr<ConfigUriComponentsBackend> backend_;
    bool windows_;
    mutable std::recursive_mutex mutex_;
    mutable Text parse_, host_, absolute_, display_, fragment_, query_, path_;
    mutable std::optional<std::u16string> scope_;
    void remaining() const {
        if (!(state_.flags & 0x80000000)) {
            bool normalize = (state_.flags & (0x200000000ull | 0x800000000ull)) == 0x200000000ull;
            try {
                config_file_uri_parse_remaining(state_);
            } catch (const ConfigFileUriStateError& e) {
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
        auto type = s.flags & 0x70000;
        auto raw = (type == 0x50000 && (s.flags & 0x8000000)) || type == 0x70000
                       ? std::u16string{}
                       : *substring(parse_, o.host, static_cast<std::int64_t>(o.path) - o.host);
        std::u16string value;
        bool loopback = false;
        if (type == 0x30000 || type == 0x40000) {
            auto colon = raw.rfind(u':');
            auto upper = raw.find_first_of(u"ABCDEFGHIJKLMNOPQRSTUVWXYZ");
            if (colon != raw.npos) {
                raw.resize(colon);
                upper = raw.find_first_of(u"ABCDEFGHIJKLMNOPQRSTUVWXYZ");
            }
            if (upper != raw.npos) {
                if (!backend_->lower_invariant)
                    throw std::invalid_argument("File URI requires source invariant lower provider");
                value = *backend_->lower_invariant(text(raw));
            } else if (raw == u"localhost" || raw == u"loopback") {
                loopback = true;
                value = u"localhost";
            } else
                value = raw;
        } else if (type == 0x20000) {
            auto v = ipv4(raw);
            if (!v)
                parsing_error(8);
            value = *v;
            loopback = value.starts_with(u"127.");
        } else if (type == 0x10000) {
            auto v = ipv6(raw);
            value = std::move(v.first);
            if (!v.second.empty())
                scope_ = std::move(v.second);
            loopback = value == u"[::1]" || value == u"[::127.0.0.1]" || value == u"[::ffff:127.0.0.1]";
        } else if (type == 0x50000) {
            value = (s.flags & 0x8000000) ? u"" : raw;
            loopback = value.empty();
        } else if (type != 0x70000)
            parsing_error(8);
        if (loopback)
            s.flags |= 0x400000;
        if (!value.empty()) {
            if (type == 0x50000) { /* Valid file Basic hosts are empty. */
            } else if (!(s.flags & 0x2000000)) {
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
        auto flags = state_.flags;
        unsigned result =
            format == 1 ? static_cast<unsigned>((flags & 0x3f80) >> 6) : static_cast<unsigned>(flags & 127);
        if (format == 1 && (flags & 1))
            result |= 1;
        if (flags & (0x2000 | 0x4000 | 0x8000))
            result |= 16;
        else if ((flags & 0x8000000)) {
            auto p = state_.offsets.path;
            auto secured = (state_.parse_string[p] == u'/' || state_.parse_string[p] == u'\\') ? 3 : 2;
            if (state_.parse_string[p + secured - 1] == u'|')
                result |= 16;
        }
        return result;
    }
    std::u16string component(bool fragment, int format, unsigned noncanonical) const {
        auto o = state_.offsets;
        auto start = fragment ? o.fragment : o.query;
        auto end = fragment ? o.end : o.fragment;
        if (start == end)
            return {};
        auto raw = V(state_.parse_string).substr(start + 1, end - start - 1);
        auto bit = fragment ? 64u : 32u;
        int mode = 0;
        if (noncanonical & bit) {
            if (format == 1 && !(state_.flags & 0x80000))
                return config_uri_escape(raw, true,
                                         fragment ? ConfigUriEscapeSet::UnreservedReserved
                                                  : ConfigUriEscapeSet::ExceptHash);
            if (format != 1)
                mode = format == 32767 ? ((state_.flags & 0x80000) ? 6 : 7)
                       : format == 2   ? 10
                                       : ((state_.flags & 0x80000) ? 2 : 3);
        }
        return config_uri_unescape(raw, u'#', 0xffff, 0xffff, mode, true, !fragment);
    }
    Text whole(int format, unsigned parts = 127) const {
        remaining();
        host();
        auto noncanonical = mask(format);
        auto o = state_.offsets;
        if ((noncanonical & parts) == 0) {
            auto end = (parts & 64) ? o.end : (parts & 32) ? o.fragment : (parts & 16) ? o.query : o.path;
            return substring(parse_, o.scheme, end - o.scheme);
        }
        std::u16string dest = u"file:";
        if (state_.flags & 0x100000)
            dest += u"//";
        int hostmode = (format != 1 && (state_.flags & 0x70000) == 0x50000 && (noncanonical & 4))
                           ? (format == 2 ? 10 : ((state_.flags & 0x80000) ? 2 : 3))
                           : 0;
        dest += config_uri_unescape(*host_, u'/', u'?', u'#', hostmode, true, false);
        if (parts & 16)
            dest = config_file_uri_canonical_path(state_, std::move(dest), format, windows_);
        if ((parts & 32) && o.query < o.fragment) {
            dest += u'?';
            dest += component(false, format, noncanonical);
        }
        if ((parts & 64) && o.fragment < o.end) {
            dest += u'#';
            dest += component(true, format, noncanonical);
        }
        return text(std::move(dest));
    }

  public:
    FileUriObject(Text original, ConfigFileUriState state,
                  std::shared_ptr<ConfigUriComponentsBackend> backend, bool windows, Text parse = {})
        : state_(std::move(state)), original_(std::move(original)), backend_(std::move(backend)),
          windows_(windows) {
        parse_ = parse                             ? std::move(parse)
                 : (state_.flags & 0x200000000ull) ? text(state_.parse_string)
                                                   : original_;
    }

    ConfigUriObjectPtr clone_source() const {
        std::lock_guard lock(mutex_);
        auto state = state_;
        if (state.flags & 0x40000000ull) {
            state.flags &= ~(0xffffull | 0x40000000ull | 0x80000000ull);
            state.flags |= state.offsets.path;
        }
        state.info = false;
        state.offsets = {};
        return std::make_shared<FileUriObject>(original_, std::move(state), backend_, windows_, parse_);
    }
    U flags_source() const {
        std::lock_guard lock(mutex_);
        return state_.flags;
    }
    bool unix_syntax_source() const {
        return state_.unix_file_syntax;
    }
    bool windows_source() const {
        return windows_;
    }
    Text parse_source() const {
        std::lock_guard lock(mutex_);
        return parse_;
    }
    std::size_t secured_source() const {
        std::lock_guard lock(mutex_);
        remaining();
        if (!(state_.flags & 0x8000000))
            return 0;
        auto c = state_.parse_string[state_.offsets.path];
        return c == u'/' || c == u'\\' ? 3 : 2;
    }
    Text prefix_parts(int format, unsigned parts) const {
        std::lock_guard lock(mutex_);
        return whole(format, parts);
    }
    Text host_parts() const {
        std::lock_guard lock(mutex_);
        remaining();
        host();
        return host_;
    }
    Text path_parts(int format) const {
        std::lock_guard lock(mutex_);
        remaining();
        auto o = state_.offsets;
        if (!(mask(format) & 16))
            return substring(parse_, o.path, o.query - o.path);
        host();
        return text(config_file_uri_canonical_path(state_, u"", format, windows_));
    }
    Text absolute_path_parts() const {
        std::lock_guard lock(mutex_);
        if (!path_)
            path_ = path_parts(1);
        if ((state_.flags & 0x8000000) && !path_->empty() && path_->front() == u'/')
            return substring(path_, 1, path_->size() - 1);
        return path_;
    }
    Text query_parts() const {
        std::lock_guard lock(mutex_);
        remaining();
        if (!query_) {
            auto o = state_.offsets;
            auto noncanonical = mask(1);
            if (!(noncanonical & 32))
                query_ = substring(parse_, o.query, o.fragment - o.query);
            else {
                host();
                query_ = text(o.query < o.fragment ? u"?" + component(false, 1, noncanonical) : u"");
            }
        }
        return query_;
    }
    bool is_absolute() const override {
        return true;
    }
    Text original_string() const override {
        return original_;
    }
    Text scheme() const override {
        static auto file = text(u"file");
        return file;
    }
    Text absolute_uri() const override {
        std::lock_guard lock(mutex_);
        if (!absolute_)
            absolute_ = whole(1);
        return absolute_;
    }
    Text to_string() const override {
        std::lock_guard lock(mutex_);
        if (!display_)
            display_ = whole(32767);
        return display_;
    }
    Text fragment() const override {
        std::lock_guard lock(mutex_);
        remaining();
        if (!fragment_) {
            auto o = state_.offsets;
            auto noncanonical = mask(1);
            if (!(noncanonical & 64))
                fragment_ = substring(parse_, o.fragment, o.end - o.fragment);
            else {
                host();
                fragment_ = text(o.fragment < o.end ? u"#" + component(true, 1, noncanonical) : u"");
            }
        }
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
                std::size_t start = unc ? o.host - 2 : o.path;
                Text value;
                if (implicit && o.host == (dos ? 0 : 2) && o.query == o.end)
                    value = parse_;
                else {
                    if (dos && (s.parse_string[start] == u'/' || s.parse_string[start] == u'\\'))
                        ++start;
                    value = substring(parse_, start, static_cast<std::int64_t>(o.query) - start);
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
            std::size_t start = o.path;
            std::u16string dest;
            if (unc)
                dest = u"\\\\" + *host_;
            else if (s.parse_string[start] == u'/' || s.parse_string[start] == u'\\')
                ++start;
            auto secured = dest.size();
            int mode = (s.flags & 16) && !implicit ? 10 : 0;
            dest += config_uri_unescape(V(s.parse_string).substr(start, o.query - start), 0xffff, 0xffff,
                                        0xffff, mode, true, true);
            if (dest.size() > 1 && dest[1] == u'|')
                dest[1] = u':';
            if (s.flags & 0x2000) {
                auto prefix = secured + (dos ? 2 : 0);
                auto compressed = config_file_uri_compress(dest.substr(prefix), !s.unix_file_syntax, true);
                dest.resize(prefix);
                dest += compressed;
            }
            for (auto& c : dest)
                if (c == u'/')
                    c = u'\\';
            return text(std::move(dest));
        }
        auto noncanonical = mask(2);
        if (!(noncanonical & 16))
            return substring(parse_, o.path, o.query - o.path);
        host();
        return text(config_file_uri_canonical_path(s, u"", 2, windows_));
    }
};
}
bool config_file_uri_applies(const ConfigFileUriSchemeScan& scan) {
    return scan.scheme == u"file" || scan.error == 7;
}
ConfigUriObjectPtr config_file_uri_create(ConfigUriText input, ConfigUriObjectKind kind,
                                          std::shared_ptr<ConfigUriComponentsBackend> backend, bool windows) {
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
    auto scan = config_file_uri_scan_scheme(*input, windows);
    if (scan.error)
        parsing_error(scan.error);
    if (!config_file_uri_applies(scan))
        throw std::invalid_argument("File URI provider cannot handle generic schemes");
    if (config_file_uri_implicit_kind_is_relative(scan, *input, k, windows))
        return config_uri_relative_object(input, input);
    if (!backend || !backend->unicode.normalize_nfc)
        throw std::invalid_argument("Native file URI requires exact Unicode providers");
    auto flags = scan.flags;
    bool iri = config_uri_has_iri_characters(*input);
    if (iri)
        flags |= 0x200000000ull;
    auto parsed = config_file_uri_parse_minimal(*input, *input, flags, windows, scan.unix_file_syntax,
                                                backend->unicode);
    int error = parsed.error;
    if (error && k != 1 && error <= 4)
        return config_uri_relative_object(input, input);
    if (!error && k == 2)
        error = 12;
    ConfigFileUriState state;
    state.flags = parsed.flags;
    state.parse_string = std::move(parsed.parse_string);
    state.original_string = *input;
    state.unix_file_syntax = parsed.unix_file_syntax;
    if (iri)
        try {
            config_file_uri_parse_remaining(state);
        } catch (const ConfigFileUriStateError& e) {
            throw ConfigUriComponentsError(e.source_type, utf16_replacing(e.what()), e.hresult);
        }
    if (error)
        parsing_error(error);
    return std::make_shared<FileUriObject>(input, std::move(state), std::move(backend), windows);
}
ConfigUriObjectPtr config_file_uri_try_create(ConfigUriText input, ConfigUriObjectKind kind,
                                              std::shared_ptr<ConfigUriComponentsBackend> backend,
                                              bool windows) {
    if (!input)
        return {};
    try {
        return config_file_uri_create(std::move(input), kind, std::move(backend), windows);
    } catch (const ConfigUriComponentsError& e) {
        if (e.source_type == "System.UriFormatException")
            return {};
        throw;
    }
}

namespace {
void validate_base(const ConfigUriObjectPtr& base) {
    if (!base)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'baseUri')", 0x80004003,
                                       u"baseUri");
    if (!base->is_absolute())
        throw ConfigUriComponentsError(
            "System.ArgumentOutOfRangeException",
            u"Specified argument was out of the range of valid values. (Parameter 'baseUri')", 0x80131502,
            u"baseUri");
}
std::shared_ptr<const FileUriObject> as_file(ConfigUriObjectPtr uri) {
    auto result = std::dynamic_pointer_cast<const FileUriObject>(uri);
    if (!result)
        throw std::invalid_argument("File URI operation requires its native file object");
    return result;
}
bool whitespace(char16_t c) {
    return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n';
}
bool slash(char16_t c) {
    return c == u'/' || c == u'\\';
}
bool alpha(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}
Text trimmed(Text value) {
    std::size_t start = 0, end = value->size();
    while (start < end && whitespace((*value)[start]))
        ++start;
    while (end > start && whitespace((*value)[end - 1]))
        --end;
    return substring(value, start, end - start);
}
ConfigUriObjectPtr resolve_file(std::shared_ptr<const FileUriObject> base, Text relative,
                                const ConfigFileUriCreate& create) {
    auto value = trimmed(std::move(relative));
    auto flags = base->flags_source();
    bool implicit = (flags & 0x20000000) != 0, dos = (flags & 0x8000000) != 0,
         unc = (flags & 0x10000000) != 0;
    if (value->empty())
        return create(base->prefix_parts(3, 127), ConfigUriObjectKind::Absolute);
    if (value->front() == u'#' && !implicit)
        return create(text(*base->prefix_parts(1, 63) + *value), ConfigUriObjectKind::Absolute);
    if (value->front() == u'?' && !implicit)
        return create(text(*base->prefix_parts(1, 31) + *value), ConfigUriObjectKind::Absolute);
    if (value->size() >= 3 && ((*value)[1] == u':' || (*value)[1] == u'|') && alpha((*value)[0]) &&
        slash((*value)[2])) {
        if (implicit)
            return create(value, ConfigUriObjectKind::Absolute);
        return create(text(u"file:///" + *value), ConfigUriObjectKind::Absolute);
    }
    for (std::size_t i = 0; i < value->size() && (*value)[i] != u'/' && (*value)[i] != u'\\' &&
                            (*value)[i] != u'?' && (*value)[i] != u'#';
         ++i)
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
            std::u16string scheme = value->substr(0, i);
            for (auto& c : scheme)
                if (c >= u'A' && c <= u'Z')
                    c += 32;
            if (scheme == u"file" && !base->unix_syntax_source())
                value = substring(value, i + 1, value->size() - i - 1);
            else
                return create(value, ConfigUriObjectKind::Absolute);
            break;
        }
    if (value->empty()) {
        auto original = base->original_string();
        if (original == base->parse_source())
            return base->clone_source();
        return create(original, ConfigUriObjectKind::Absolute);
    }
    auto c = value->front();
    if (dos && slash(c) && (value->size() == 1 || !slash((*value)[1]))) {
        auto original = base->original_string();
        auto at = original->find(u':');
        if (!implicit)
            at = original->find(u':', at == original->npos ? 0 : at + 1);
        auto prefix = at == original->npos ? std::u16string{} : original->substr(0, at + 1);
        return create(text(prefix + *value), ConfigUriObjectKind::Absolute);
    }
    if (slash(c)) {
        if (value->size() >= 2 && slash((*value)[1])) {
            if (implicit)
                return create(value, ConfigUriObjectKind::Absolute);
            return create(text(u"file:" + *value), ConfigUriObjectKind::Absolute);
        }
        if (unc) {
            auto path = *base->path_parts(2);
            auto stop = path.size() > 0 ? path.find(u'/', 1) : path.npos;
            if (stop != path.npos)
                path.resize(stop);
            return create(text((implicit ? u"\\\\" : u"file://") + *base->host_parts() + path + *value),
                          ConfigUriObjectKind::Absolute);
        }
        return create(text(u"file://" + *value), ConfigUriObjectKind::Absolute);
    }
    auto path = *base->path_parts(2);
    auto directory = path.rfind(u'/');
    path.resize(directory == path.npos ? 0 : directory + 1);
    auto end = value->find(u'?');
    if (!implicit)
        end = std::min(end, value->find(u'#'));
    if (end == value->npos)
        end = value->size();
    std::u16string body = path + value->substr(0, end), suffix = value->substr(end);
    if (implicit && dos) {
        auto compressed = config_file_uri_compress(body.substr(3), !base->unix_syntax_source(), true);
        body.resize(3);
        body += compressed;
        return create(text(body.substr(1) + suffix), ConfigUriObjectKind::Absolute);
    }
    std::u16string prefix;
    if (implicit) {
        if (base->windows_source() || !(flags & 0x100000000000ull))
            prefix = u"\\\\";
        prefix += *base->host_parts();
    } else
        prefix = *base->prefix_parts(3, 15);
    auto secured = base->secured_source();
    auto compressed = config_file_uri_compress(body.substr(secured), !base->unix_syntax_source(), true);
    body.resize(secured);
    body += compressed;
    return create(text(prefix + body + suffix), ConfigUriObjectKind::Absolute);
}
}
ConfigUriObjectPtr config_file_uri_combine_string(ConfigUriObjectPtr base, ConfigUriText relative,
                                                  const ConfigFileUriCreate& operation) {
    validate_base(base);
    auto create = operation;
    if (!create)
        throw std::invalid_argument("File URI combine requires global constructor dispatch");
    auto parsed = create(relative ? relative : runtime_empty_text(), ConfigUriObjectKind::RelativeOrAbsolute);
    if (parsed->is_absolute())
        return parsed;
    return resolve_file(as_file(std::move(base)), parsed->original_string(), create);
}
ConfigUriObjectPtr config_file_uri_combine_uri(ConfigUriObjectPtr base, ConfigUriObjectPtr relative,
                                               const ConfigFileUriCreate& operation) {
    validate_base(base);
    if (!relative)
        throw ConfigUriComponentsError("System.NullReferenceException",
                                       u"Object reference not set to an instance of an object.", 0x80004003);
    if (relative->is_absolute())
        return as_file(std::move(relative))->clone_source();
    auto create = operation;
    if (!create)
        throw std::invalid_argument("File URI combine requires global constructor dispatch");
    return resolve_file(as_file(std::move(base)), relative->original_string(), create);
}
ConfigUriObjectPtr config_file_uri_with_fragment(ConfigUriObjectPtr uri, ConfigUriText fragment,
                                                 const ConfigFileUriCreate& operation) {
    if (!uri)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'uri')", 0x80004003, u"uri");
    uri->scheme();
    auto file = as_file(uri);
    auto host = file->host_parts();
    auto path = file->absolute_path_parts();
    auto query = file->query_parts();
    file->fragment();
    std::u16string value = u"file://" + *host;
    if (!host->empty() && !path->empty() && path->front() != u'/')
        value += u'/';
    value += *path;
    value += *query;
    if (fragment && !fragment->empty()) {
        if (fragment->front() != u'#')
            value += u'#';
        value += *fragment;
    }
    auto create = operation;
    if (!create)
        throw std::invalid_argument("File URI builder requires global constructor dispatch");
    auto result = create(text(std::move(value)), ConfigUriObjectKind::Absolute);
    auto native = as_file(result);
    native->scheme();
    native->host_parts();
    native->absolute_path_parts();
    native->query_parts();
    native->fragment();
    return result;
}
}

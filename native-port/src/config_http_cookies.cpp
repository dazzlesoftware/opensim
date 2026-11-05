// .NET8.0.31 System.Net cookie consumed closure; MIT .NET license.
#include "opensim/framework/config_http_cookies.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <mutex>
namespace opensim::framework {
ConfigHttpCookieError::ConfigHttpCookieError(std::string type, std::u16string text, std::uint32_t h,
                                             std::optional<std::u16string> p, std::exception_ptr i)
    : runtime_error(utf8(text)), source_type(std::move(type)), message(std::move(text)), hresult(h),
      parameter(std::move(p)), inner(std::move(i)) {
}
namespace {
using Text = ConfigCookieText;
using V = std::u16string_view;
using Backend = ConfigHttpCookiesBackend;
std::u16string newline() {
#ifdef _WIN32
    return u"\r\n";
#else
    return u"\n";
#endif
}
constexpr std::int64_t MaxTicks = 3155378975999999999LL;
Text t(std::u16string s) {
    return std::make_shared<const std::u16string>(std::move(s));
}
std::u16string number(std::int64_t n) {
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
[[noreturn]] void cookie_error(std::u16string message, std::exception_ptr inner = {}) {
    throw ConfigHttpCookieError("System.Net.CookieException", std::move(message), 0x80131537, {},
                                std::move(inner));
}
[[noreturn]] void null(std::u16string p) {
    throw ConfigHttpCookieError("System.ArgumentNullException",
                                u"Value cannot be null. (Parameter '" + p + u"')", 0x80004003, p);
}
[[noreturn]] void attribute(V name, V value) {
    cookie_error(u"The '" + std::u16string(name) + u"'='" + std::u16string(value) +
                 u"' part of the cookie is invalid.");
}
V unquote(V s) {
    return s.size() >= 2 && s.front() == u'"' && s.back() == u'"' ? s.substr(1, s.size() - 2) : s;
}
bool quoted(V s) {
    return s.size() > 2 && s.front() == u'"' && s.back() == u'"';
}
bool reserved_name(V s) {
    return s.find_first_of(u"\t\r\n=;,") != V::npos;
}
bool valid_name(V s) {
    return !s.empty() && s.front() != u'$' && s.front() != u' ' && s.back() != u' ' && !reserved_name(s);
}
bool domain_chars(V s) {
    if (s.empty())
        return false;
    for (auto c : s)
        if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-' ||
              c == u'.' || c == u'_'))
            return false;
    return true;
}
bool eq(const Backend& b, V a, V c) {
    return b.ordinal_ignore_case_equal(a, c);
}
struct Cookie : ConfigHttpCookieView {
    Text key;
};
enum Token {
    Nothing,
    NameValue,
    Attribute,
    EndToken,
    EndCookie,
    End,
    Equals,
    Comment,
    CommentUrl,
    CookieName,
    Discard,
    Domain,
    Expires,
    MaxAge,
    Path,
    Port,
    Secure,
    HttpOnly,
    Unknown,
    Version
};
struct Tokenizer {
    const Backend& backend;
    Text stream;
    std::size_t index = 0, start = 0;
    std::int32_t length = 0;
    bool end_cookie = false, quoted_ = false;
    Token token = Nothing;
    std::u16string name, value;
    Tokenizer(Text s, const Backend& b) : backend(b), stream(std::move(s)) {
    }
    bool eof() const {
        return index >= stream->size();
    }
    std::u16string extract() {
        if (!length)
            return {};
        auto value = stream->substr(start, static_cast<std::size_t>(length));
        if (!quoted_) {
            std::size_t a = 0, b = value.size();
            while (a < b && backend.white_space(value[a]))
                ++a;
            while (b > a && backend.white_space(value[b - 1]))
                --b;
            value = value.substr(a, b - a);
        }
        return value;
    }
    Token find(bool ignore_comma, bool ignore_equals) {
        length = 0;
        start = index;
        while (!eof() && backend.white_space((*stream)[index])) {
            ++index;
            ++start;
        }
        Token result = End;
        int increment = 1;
        if (!eof()) {
            if ((*stream)[index] == u'"') {
                quoted_ = true;
                ++index;
                bool escaped = false;
                while (!eof()) {
                    auto c = (*stream)[index];
                    if (!escaped && c == u'"')
                        break;
                    if (escaped)
                        escaped = false;
                    else if (c == u'\\')
                        escaped = true;
                    ++index;
                }
                if (!eof())
                    ++index;
                length = static_cast<std::int32_t>(index - start);
                increment = 0;
                ignore_comma = false;
            }
            while (!eof() && (*stream)[index] != u';' && (ignore_equals || (*stream)[index] != u'=') &&
                   (ignore_comma || (*stream)[index] != u',')) {
                if ((*stream)[index] == u',') {
                    start = index + 1;
                    length = -1;
                    ignore_comma = false;
                }
                ++index;
                length += increment;
            }
            if (!eof()) {
                auto c = (*stream)[index++];
                result = c == u';' ? EndToken : c == u'=' ? Equals : EndCookie;
            }
        }
        return result;
    }
    Token attribute_token() {
        static constexpr std::pair<V, Token> names[] = {
            {u"Path", Path},       {u"Max-Age", MaxAge},        {u"Expires", Expires},  {u"Version", Version},
            {u"Domain", Domain},   {u"Secure", Secure},         {u"Discard", Discard},  {u"Port", Port},
            {u"Comment", Comment}, {u"CommentURL", CommentUrl}, {u"HttpOnly", HttpOnly}};
        for (auto& [text, type] : names)
            if (eq(backend, name, text))
                return type;
        return Unknown;
    }
    Token next(bool first) {
        end_cookie = false;
        name.clear();
        value.clear();
        quoted_ = false;
        token = Nothing;
        length = 0;
        start = index;
        auto found = find(false, false);
        if (found == EndCookie)
            end_cookie = true;
        if (found == End || found == EndCookie) {
            name = extract();
            if (!name.empty()) {
                token = attribute_token();
                return Attribute;
            }
            return found;
        }
        name = extract();
        token = first ? CookieName : attribute_token();
        if (found == Equals) {
            found = find(!first && token == Expires, true);
            if (found == EndCookie)
                end_cookie = true;
            value = extract();
            return NameValue;
        }
        return Attribute;
    }
};
void set_port(Cookie& cookie, V value, const Backend& backend) {
    if (value.empty()) {
        cookie.port_implicit = true;
        cookie.port = t(u"");
        return;
    }
    cookie.port_implicit = false;
    if (value.front() != u'"' || value.back() != u'"')
        attribute(u"Port", value);
    std::vector<std::int32_t> ports;
    std::size_t at = 0;
    while (at < value.size()) {
        while (at < value.size() && (value[at] == u' ' || value[at] == u',' || value[at] == u'"'))
            ++at;
        auto end = at;
        while (end < value.size() && value[end] != u' ' && value[end] != u',' && value[end] != u'"')
            ++end;
        if (end > at) {
            auto result = backend.try_int(t(std::u16string(value.substr(at, end - at))));
            if (!result || *result < 0 || *result > 65535)
                attribute(u"Port", value);
            ports.push_back(*result);
        }
        at = end;
    }
    cookie.ports = std::move(ports);
    cookie.port = t(std::u16string(value));
    cookie.version = 1;
    cookie.variant = 3;
}
class Parser {
    Tokenizer tokens_;
    const Backend& backend_;

  public:
    Parser(Text input, const Backend& b) : tokens_(std::move(input), b), backend_(b) {
    }
    bool eof() const {
        return tokens_.eof();
    }
    std::shared_ptr<Cookie> get() {
        std::shared_ptr<Cookie> cookie;
        std::array<bool, 10> seen{};
        do {
            auto found = tokens_.next(!cookie);
            if (!cookie && (found == NameValue || found == Attribute)) {
                cookie = std::make_shared<Cookie>();
                cookie->name = t(valid_name(tokens_.name) ? tokens_.name : u"");
                cookie->value = t(tokens_.value);
                cookie->domain = cookie->path = cookie->port = cookie->comment = cookie->key = t(u"");
                cookie->variant = 1;
                cookie->created = backend_.now();
                continue;
            }
            if (!cookie)
                continue;
            if (found == NameValue) {
                auto& value = tokens_.value;
                switch (tokens_.token) {
                case Comment:
                    if (!seen[0]) {
                        seen[0] = true;
                        cookie->comment = t(value);
                    }
                    break;
                case CommentUrl:
                    seen[1] = true;
                    break; // CommentUri does not affect consumed matching/rendering.
                case Domain:
                    if (!seen[2]) {
                        seen[2] = true;
                        cookie->domain = t(std::u16string(unquote(value)));
                        cookie->domain_implicit = false;
                        cookie->quoted_domain = tokens_.quoted_;
                    }
                    break;
                case Expires:
                case MaxAge:
                    if (!seen[3]) {
                        seen[3] = true;
                        if (tokens_.token == Expires) {
                            auto date = backend_.parse_expiry(t(std::u16string(unquote(value))));
                            if (date)
                                cookie->expires = *date;
                            else
                                cookie->name = t(u"");
                        } else {
                            auto seconds = backend_.try_int(t(std::u16string(unquote(value))));
                            if (!seconds)
                                cookie->name = t(u"");
                            else {
                                auto date = backend_.now();
                                auto ticks = date.ticks() + std::int64_t(*seconds) * 10000000;
                                if (ticks < 0 || ticks > MaxTicks)
                                    throw ConfigHttpCookieError(
                                        "System.ArgumentOutOfRangeException",
                                        u"The added or subtracted value results in an un-representable DateTime. (Parameter 'value')",
                                        0x80131502, u"value");
                                cookie->expires = AssetDateTime(ticks, date.kind());
                            }
                        }
                    }
                    break;
                case Path:
                    if (!seen[4]) {
                        seen[4] = true;
                        cookie->path = t(value);
                        cookie->path_implicit = false;
                    }
                    break;
                case Port:
                    if (!seen[5]) {
                        seen[5] = true;
                        try {
                            set_port(*cookie, value, backend_);
                        } catch (...) {
                            cookie->name = t(u"");
                        }
                    }
                    break;
                case Version:
                    if (!seen[6]) {
                        seen[6] = true;
                        auto version = backend_.try_int(t(std::u16string(unquote(value))));
                        if (!version)
                            cookie->name = t(u"");
                        else {
                            if (*version < 0)
                                throw ConfigHttpCookieError(
                                    "System.ArgumentOutOfRangeException",
                                    u"value ('" + number(*version) +
                                        u"') must be a non-negative value. (Parameter 'value')" + newline() +
                                        u"Actual value was " + number(*version) + u".",
                                    0x80131502, u"value");
                            cookie->version = *version;
                            if (*version > 0 && cookie->variant < 2)
                                cookie->variant = 2;
                            cookie->quoted_version = tokens_.quoted_;
                        }
                    }
                    break;
                default:
                    break;
                }
            } else if (found == Attribute) {
                switch (tokens_.token) {
                case Discard:
                    if (!seen[8]) {
                        seen[8] = true;
                        cookie->discard = true;
                    }
                    break;
                case Secure:
                    if (!seen[7]) {
                        seen[7] = true;
                        cookie->secure = true;
                    }
                    break;
                case HttpOnly:
                    cookie->http_only = true;
                    break;
                case Port:
                    if (!seen[5]) {
                        seen[5] = true;
                        set_port(*cookie, u"", backend_);
                    }
                    break;
                default:
                    break;
                }
            }
        } while (!tokens_.eof() && !tokens_.end_cookie);
        return cookie;
    }
};
bool equal_domains(const Backend& b, V a, V c) {
    if (!a.empty() && a.front() == u'.')
        a.remove_prefix(1);
    if (!c.empty() && c.front() == u'.')
        c.remove_prefix(1);
    return eq(b, a, c);
}
bool same_cookie(const Backend& b, const Cookie& a, const Cookie& c) {
    return eq(b, *a.name, *c.name) && equal_domains(b, *a.domain, *c.domain) && *a.path == *c.path;
}
int add_collection(std::vector<std::shared_ptr<Cookie>>& list, std::shared_ptr<Cookie> cookie,
                   const Backend& b) {
    for (auto& old : list)
        if (same_cookie(b, *old, *cookie)) {
            if (old->variant <= cookie->variant)
                old = std::move(cookie);
            return 0;
        }
    list.push_back(std::move(cookie));
    return 1;
}
std::u16string render(const Cookie& c) {
    std::u16string value;
    if (c.version) {
        value = u"$Version=";
        if (c.quoted_version)
            value += u'"';
        value += number(c.version);
        if (c.quoted_version)
            value += u'"';
        value += u"; ";
    }
    value += *c.name + u"=" + *c.value;
    if (c.variant != 1) {
        if (!c.path_implicit && !c.path->empty())
            value += u"; $Path=" + *c.path;
        if (!c.domain_implicit && !c.domain->empty()) {
            value += u"; $Domain=";
            if (c.quoted_domain)
                value += u'"';
            value += *c.domain;
            if (c.quoted_domain)
                value += u'"';
        }
    }
    if (!c.port_implicit) {
        value += u"; $Port";
        if (!c.port->empty())
            value += u"=" + *c.port;
    }
    return value == u"=" ? u"" : value;
}
struct Collection {
    std::vector<std::shared_ptr<Cookie>> cookies;
    std::int64_t stamp = 0;
};
struct PathEntry {
    Text path;
    std::shared_ptr<Collection> collection;
};
int path_compare(V a, V b) {
    a = unquote(a);
    b = unquote(b);
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        if (a[i] != b[i])
            return int(a[i]) - int(b[i]);
    return int(b.size()) - int(a.size());
}
struct DomainPaths {
    std::vector<PathEntry> paths;
    std::shared_ptr<Collection> get(Text path, bool create) {
        std::size_t at = 0;
        while (at < paths.size() && path_compare(*paths[at].path, *path) < 0)
            ++at;
        if (at < paths.size() && !path_compare(*paths[at].path, *path))
            return paths[at].collection;
        if (!create)
            return {};
        auto result = std::make_shared<Collection>();
        paths.insert(paths.begin() + at, PathEntry{std::move(path), result});
        return result;
    }
    int count() const {
        int n = 0;
        for (auto& p : paths)
            n += static_cast<int>(p.collection->cookies.size());
        return n;
    }
};
// Hashtable's source buckets/iteration, with an explicitly supplied string hash seed/provider.
class DomainTable {
    struct Bucket {
        Text key;
        std::shared_ptr<DomainPaths> value;
        std::uint32_t hash = 0;
        bool tomb = false;
    };
    std::vector<Bucket> buckets_{3};
    int count_ = 0, occupancy_ = 0, load_ = 2;
    const Backend& backend_;
    static int prime(int minimum) {
        static constexpr int primes[] = {
            3,       7,       11,      17,      23,      29,     37,      47,      59,      71,      89,
            107,     131,     163,     197,     239,     293,    353,     431,     521,     631,     761,
            919,     1103,    1327,    1597,    1931,    2333,   2801,    3371,    4049,    4861,    5839,
            7013,    8419,    10103,   12143,   14591,   17519,  21023,   25229,   30293,   36353,   43627,
            52361,   62851,   75431,   90523,   108631,  130363, 156437,  187751,  225307,  270371,  324449,
            389357,  467237,  560689,  672827,  807403,  968897, 1162687, 1395263, 1674319, 2009191, 2411033,
            2893249, 3471899, 4166287, 4999559, 5999471, 7199369};
        for (auto n : primes)
            if (n >= minimum)
                return n;
        for (int n = minimum | 1; n < std::numeric_limits<int>::max(); n += 2) {
            bool valid = true;
            for (int d = 3; std::int64_t(d) * d <= n; d += 2)
                if (n % d == 0) {
                    valid = false;
                    break;
                }
            if (valid && (n - 1) % 101 != 0)
                return n;
        }
        return minimum;
    }
    void rehash(int size) {
        auto old = std::move(buckets_);
        buckets_.clear();
        buckets_.resize(size);
        occupancy_ = 0;
        for (auto& item : old)
            if (item.key) {
                auto h = item.hash & 0x7fffffff, step = 1 + h * 101u % static_cast<unsigned>(size - 1);
                auto at = h % size;
                while (buckets_[at].key) {
                    if (!(buckets_[at].hash & 0x80000000)) {
                        buckets_[at].hash |= 0x80000000;
                        ++occupancy_;
                    }
                    at = (at + step) % size;
                }
                auto collision = buckets_[at].hash;
                buckets_[at] = item;
                buckets_[at].hash = collision | h;
                buckets_[at].tomb = false;
            }
        load_ = static_cast<int>(0.72f * static_cast<float>(size));
    }

  public:
    explicit DomainTable(const Backend& b) : backend_(b) {
    }
    int count() const {
        return count_;
    }
    std::shared_ptr<DomainPaths> get(Text key) {
        auto h = backend_.domain_hash(key) & 0x7fffffff,
             step = 1 + h * 101u % static_cast<unsigned>(buckets_.size() - 1);
        auto at = h % buckets_.size();
        for (std::size_t n = 0; n < buckets_.size(); ++n) {
            auto& b = buckets_[at];
            if (b.key && (b.hash & 0x7fffffff) == h && *b.key == *key)
                return b.value;
            if (!(b.hash & 0x80000000))
                break;
            at = (at + step) % buckets_.size();
        }
        return {};
    }
    void put(Text key, std::shared_ptr<DomainPaths> value) {
        if (count_ >= load_)
            rehash(prime(static_cast<int>(buckets_.size() * 2)));
        else if (occupancy_ > load_ && count_ > 100)
            rehash(static_cast<int>(buckets_.size()));
        auto h = backend_.domain_hash(key) & 0x7fffffff,
             step = 1 + h * 101u % static_cast<unsigned>(buckets_.size() - 1);
        auto at = h % buckets_.size();
        std::optional<std::size_t> free;
        for (std::size_t n = 0; n < buckets_.size(); ++n) {
            auto& b = buckets_[at];
            if (!free && b.tomb && (b.hash & 0x80000000))
                free = at;
            if (!b.key && (!b.tomb || !(b.hash & 0x80000000))) {
                if (free)
                    at = *free;
                auto& dest = buckets_[at];
                dest.key = std::move(key);
                dest.value = std::move(value);
                dest.tomb = false;
                dest.hash |= h;
                ++count_;
                return;
            }
            if (b.key && (b.hash & 0x7fffffff) == h && *b.key == *key) {
                b.value = std::move(value);
                return;
            }
            if (!free && !(b.hash & 0x80000000)) {
                b.hash |= 0x80000000;
                ++occupancy_;
            }
            at = (at + step) % buckets_.size();
        }
        if (free) {
            auto& b = buckets_[*free];
            b.key = std::move(key);
            b.value = std::move(value);
            b.tomb = false;
            b.hash |= h;
            ++count_;
            return;
        }
        throw std::runtime_error("Hashtable insertion failed");
    }
    void remove(Text key) {
        auto h = backend_.domain_hash(key) & 0x7fffffff,
             step = 1 + h * 101u % static_cast<unsigned>(buckets_.size() - 1);
        auto at = h % buckets_.size();
        for (std::size_t n = 0; n < buckets_.size(); ++n) {
            auto& b = buckets_[at];
            if (b.key && (b.hash & 0x7fffffff) == h && *b.key == *key) {
                b.hash &= 0x80000000;
                b.tomb = b.hash != 0;
                b.key.reset();
                b.value.reset();
                --count_;
                return;
            }
            if (!(b.hash & 0x80000000))
                return;
            at = (at + step) % buckets_.size();
        }
    }
    std::vector<std::pair<Text, std::shared_ptr<DomainPaths>>> entries() const {
        std::vector<std::pair<Text, std::shared_ptr<DomainPaths>>> out;
        for (std::size_t i = buckets_.size(); i-- > 0;)
            if (buckets_[i].key)
                out.emplace_back(buckets_[i].key, buckets_[i].value);
        return out;
    }
};
}
namespace {
void age_sort(std::vector<std::shared_ptr<Collection>>& values) {
    auto less = [](const auto& a, const auto& b) { return a->stamp < b->stamp; };
    auto swap_greater = [&](int a, int b) {
        if (a != b && less(values[b], values[a]))
            std::swap(values[a], values[b]);
    };
    std::function<void(int, int, int)> intro;
    auto heap = [&](int lo, int hi) {
        int n = hi - lo + 1;
        auto down = [&](int i, int size) {
            auto value = values[lo + i - 1];
            while (i <= size / 2) {
                int child = i * 2;
                if (child < size && less(values[lo + child - 1], values[lo + child]))
                    ++child;
                if (!less(value, values[lo + child - 1]))
                    break;
                values[lo + i - 1] = values[lo + child - 1];
                i = child;
            }
            values[lo + i - 1] = value;
        };
        for (int i = n / 2; i >= 1; --i)
            down(i, n);
        for (int i = n; i > 1; --i) {
            std::swap(values[lo], values[lo + i - 1]);
            down(1, i - 1);
        }
    };
    intro = [&](int lo, int hi, int depth) {
        while (hi > lo) {
            int n = hi - lo + 1;
            if (n <= 16) {
                if (n == 2) {
                    swap_greater(lo, hi);
                    return;
                }
                if (n == 3) {
                    swap_greater(lo, hi - 1);
                    swap_greater(lo, hi);
                    swap_greater(hi - 1, hi);
                    return;
                }
                for (int i = lo; i < hi; ++i) {
                    auto value = values[i + 1];
                    int j = i;
                    while (j >= lo && less(value, values[j])) {
                        values[j + 1] = values[j];
                        --j;
                    }
                    values[j + 1] = value;
                }
                return;
            }
            if (!depth) {
                heap(lo, hi);
                return;
            }
            --depth;
            int middle = lo + (hi - lo) / 2;
            swap_greater(lo, middle);
            swap_greater(lo, hi);
            swap_greater(middle, hi);
            auto pivot = values[middle];
            std::swap(values[middle], values[hi - 1]);
            int left = lo, right = hi - 1;
            for (;;) {
                while (less(values[++left], pivot)) {
                }
                while (less(pivot, values[--right])) {
                }
                if (left >= right)
                    break;
                std::swap(values[left], values[right]);
            }
            std::swap(values[left], values[hi - 1]);
            intro(left + 1, hi, depth);
            hi = left - 1;
        }
    };
    if (values.size() > 1) {
        int bits = 0;
        for (auto n = values.size(); n; n >>= 1)
            ++bits;
        intro(0, static_cast<int>(values.size()) - 1, 2 * bits);
    }
}
}
struct ConfigHttpCookies::State {
    std::shared_ptr<Backend> backend;
    DomainTable domains;
    int max_count = 300, max_domain = 20, max_size = 4096, count = 0;
    mutable std::recursive_mutex mutex;
    State(std::shared_ptr<Backend> b, int capacity, int per_domain, int size)
        : backend(std::move(b)), domains(*backend), max_count(capacity), max_domain(per_domain),
          max_size(size) {
    }
    bool expired(const Cookie& cookie) {
        if (cookie.expires.ticks() == 0)
            return false;
        auto local = backend->to_local_time(cookie.expires);
        auto now = backend->now();
        return local.ticks() <= now.ticks();
    }
    bool local_domain(V host) {
        auto dot = host.find(u'.');
        if (dot == V::npos)
            return true;
        if (host == u"127.0.0.1" || host == u"::1" || host == u"0:0:0:0:0:0:0:1")
            return true;
        auto fqdn = *backend->fqdn_my_domain;
        if (host.size() - dot >= fqdn.size() && eq(*backend, fqdn, host.substr(dot, fqdn.size())))
            return true;
        std::vector<V> pieces;
        std::size_t at = 0;
        while (at <= host.size()) {
            auto end = host.find(u'.', at);
            if (end == V::npos)
                end = host.size();
            pieces.push_back(host.substr(at, end - at));
            if (end == host.size())
                break;
            at = end + 1;
        }
        if (pieces.size() == 4 && pieces[0] == u"127") {
            for (std::size_t i = 1; i < 4; ++i) {
                if (pieces[i].empty() || pieces[i].size() > 3)
                    return false;
                for (auto c : pieces[i])
                    if (c < u'0' || c > u'9')
                        return false;
            }
            return true;
        }
        return false;
    }
    bool verify(Cookie& cookie, std::shared_ptr<ConfigCookieUri> uri) {
        auto host = uri->host(), path = uri->absolute_path();
        int port = uri->port();
        cookie.variant = cookie.version == 0 ? 1 : 2;
        if (!valid_name(*cookie.name))
            attribute(u"Name", *cookie.name);
        if (!quoted(*cookie.value) && cookie.value->find_first_of(u";,") != V::npos)
            attribute(u"Value", *cookie.value);
        if (!quoted(*cookie.comment) && cookie.comment->find_first_of(u";,") != V::npos)
            attribute(u"Comment", *cookie.comment);
        if (!quoted(*cookie.path) && cookie.path->find_first_of(u";,") != V::npos)
            attribute(u"Path", *cookie.path);
        if (cookie.domain_implicit) {
            cookie.domain = host;
            cookie.key = host;
        } else {
            auto domain = *cookie.domain;
            if (!domain_chars(domain))
                attribute(u"Domain", domain);
            if (domain.front() != u'.')
                domain = u'.' + domain;
            bool match = true;
            auto equal_host = [&]() {
                return domain.size() == host->size() + 1 && eq(*backend, V(domain).substr(1), *host);
            };
            bool local_match = local_domain(*host) && eq(*backend, *backend->fqdn_my_domain, domain);
            if (!local_match && domain.size() < 2)
                throw ConfigHttpCookieError(
                    "System.ArgumentOutOfRangeException",
                    u"Count must be positive and count must refer to a location within the string/array/collection. (Parameter 'count')",
                    0x80131502, u"count");
            auto interior = domain.size() >= 2 ? V(domain).substr(1, domain.size() - 2).find(u'.') : V::npos;
            if (local_match) {
            } else if (interior == V::npos) {
                if (!equal_host())
                    match = false;
            } else if (cookie.variant == 1) {
                if (!equal_host() && (host->size() <= domain.size() ||
                                      !eq(*backend, V(*host).substr(host->size() - domain.size()), domain)))
                    match = false;
            } else {
                auto dot = host->find(u'.');
                if ((dot == V::npos || domain.size() != host->size() - dot ||
                     !eq(*backend, V(*host).substr(dot), domain)) &&
                    !equal_host())
                    match = false;
            }
            if (!match)
                attribute(u"Domain", *cookie.domain);
            cookie.key = backend->lower_invariant(t(domain));
        }
        if (cookie.path_implicit) {
            auto slash = path->rfind(u'/');
            if (cookie.variant == 1)
                cookie.path =
                    t(path->empty() || path->front() != u'/' || slash == 0 ? u"/" : path->substr(0, slash));
            else if (cookie.variant == 2) {
                if (slash == V::npos)
                    throw ConfigHttpCookieError(
                        "System.ArgumentOutOfRangeException",
                        u"length ('-1') must be a non-negative value. (Parameter 'length')" + newline() +
                            u"Actual value was -1.",
                        0x80131502, u"length");
                cookie.path = t(path->substr(0, slash));
            } else
                cookie.path = t(path->substr(0, slash + 1));
        }
        if (!cookie.port_implicit && cookie.port->empty())
            cookie.ports = {port};
        if (!cookie.port_implicit &&
            std::find(cookie.ports.begin(), cookie.ports.end(), port) == cookie.ports.end())
            attribute(u"Port", *cookie.port);
        return true;
    }
    int expire_collection(Collection& collection) {
        int removed = 0;
        for (std::size_t i = collection.cookies.size(); i-- > 0;)
            if (expired(*collection.cookies[i])) {
                collection.cookies.erase(collection.cookies.begin() + i);
                ++removed;
            }
        return removed;
    }
    bool age(Text domain) {
        int removed = 0, cursor = 0;
        std::int64_t oldest = MaxTicks;
        std::shared_ptr<Collection> oldest_collection;
        float ratio = 1;
        if (count > max_count)
            ratio = static_cast<float>(max_count) / static_cast<float>(count);
        auto entries = domains.entries();
        for (auto& item : entries) {
            auto key = domain ? domain : item.first;
            auto paths = domain ? domains.get(domain) : item.second;
            if (!paths)
                throw std::runtime_error("Cookie domain disappeared during aging");
            int n = 0;
            for (auto& path : paths->paths) {
                cursor = expire_collection(*path.collection);
                removed += cursor;
                count -= cursor;
                n += static_cast<int>(path.collection->cookies.size());
                if (!path.collection->cookies.empty() && path.collection->stamp < oldest) {
                    oldest_collection = path.collection;
                    oldest = path.collection->stamp;
                }
            }
            int target = std::min(static_cast<int>(static_cast<float>(n) * ratio),
                                  std::min(max_domain, max_count) - 1);
            if (n <= target)
                continue;
            std::vector<std::shared_ptr<Collection>> ordered(paths->paths.size());
            for (auto& path : paths->paths) {
                if (cursor < 0 || static_cast<std::size_t>(cursor) >= ordered.size())
                    throw ConfigHttpCookieError("System.IndexOutOfRangeException",
                                                u"Index was outside the bounds of the array.", 0x80131508);
                ordered[cursor++] = path.collection;
            }
            age_sort(ordered);
            cursor = 0;
            for (auto& collection : ordered) {
                while (n > target && !collection->cookies.empty()) {
                    collection->cookies.erase(collection->cookies.begin());
                    --n;
                    --count;
                    ++removed;
                }
                if (n <= target)
                    break;
            }
            if (n > target && domain)
                return false;
        }
        if (domain)
            return true;
        if (removed)
            return true;
        if (oldest == MaxTicks)
            return false;
        while (count >= max_count && !oldest_collection->cookies.empty()) {
            oldest_collection->cookies.erase(oldest_collection->cookies.begin());
            --count;
        }
        return true;
    }
    void cleanup() {
        for (auto& [key, paths] : domains.entries()) {
            paths->paths.erase(std::remove_if(paths->paths.begin(), paths->paths.end(),
                                              [](const auto& p) { return p.collection->cookies.empty(); }),
                               paths->paths.end());
            if (paths->paths.empty())
                domains.remove(key);
        }
    }
    void add(std::shared_ptr<Cookie> cookie) {
        if (cookie->value->size() > static_cast<std::size_t>(max_size))
            cookie_error(u"The value size of the cookie is '" + render(*cookie) +
                         u"'. This exceeds the configured maximum size, which is '" + number(max_size) +
                         u"'.");
        try {
            auto paths = domains.get(cookie->key);
            if (!paths) {
                paths = std::make_shared<DomainPaths>();
                domains.put(cookie->key, paths);
            }
            int domain_count = paths->count();
            auto collection = paths->get(cookie->path, true);
            if (expired(*cookie)) {
                for (std::size_t i = 0; i < collection->cookies.size(); ++i)
                    if (same_cookie(*backend, *collection->cookies[i], *cookie)) {
                        collection->cookies.erase(collection->cookies.begin() + i);
                        --count;
                        break;
                    }
            } else {
                if ((domain_count >= max_domain && !age(cookie->key)) || (count >= max_count && !age({})))
                    return;
                count += add_collection(collection->cookies, std::move(cookie), *backend);
            }
            if (domains.count() > count || paths->paths.size() > static_cast<std::size_t>(max_domain))
                cleanup();
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            cookie_error(u"An error occurred when adding a cookie to the container.",
                         std::current_exception());
        }
    }
    std::vector<std::shared_ptr<Cookie>> get(std::shared_ptr<ConfigCookieUri> uri) {
        std::vector<std::shared_ptr<Cookie>> result;
        if (!count)
            return result;
        auto host = uri->host();
        int port = uri->port();
        bool secure = *uri->scheme() == u"https" || *uri->scheme() == u"wss";
        std::vector<Text> primary = {host, t(u"." + *host)}, plain;
        auto dot = host->find(u'.');
        if (dot == V::npos) {
            if (!backend->fqdn_my_domain->empty()) {
                primary.push_back(t(*host + *backend->fqdn_my_domain));
                primary.push_back(backend->fqdn_my_domain);
            }
        } else {
            primary.push_back(t(host->substr(dot)));
            if (host->size() > 2) {
                auto stop = host->rfind(u'.', host->size() - 2);
                if (stop != V::npos && stop > 0)
                    stop = host->rfind(u'.', stop - 1);
                if (stop != V::npos)
                    while (dot < stop && (dot = host->find(u'.', dot + 1)) != V::npos)
                        plain.push_back(t(host->substr(dot)));
            }
        }
        auto merge = [&](const std::vector<Text>& names, bool only_plain) {
            for (auto& name : names) {
                auto paths = domains.get(name);
                if (!paths)
                    continue;
                for (auto& path : paths->paths) {
                    auto match = unquote(*path.path);
                    auto request = *uri->absolute_path();
                    if (!V(request).starts_with(match))
                        continue;
                    if (request.size() != match.size() && (match.empty() || match.back() != u'/') &&
                        request[match.size()] != u'/')
                        continue;
                    auto& source = *path.collection;
                    source.stamp = backend->now().ticks();
                    for (std::size_t i = 0; i < source.cookies.size();) {
                        auto cookie = source.cookies[i];
                        if (expired(*cookie)) {
                            source.cookies.erase(source.cookies.begin() + i);
                            --count;
                            continue;
                        }
                        bool include = !only_plain || cookie->variant == 1;
                        if (include && !cookie->ports.empty())
                            include = std::find(cookie->ports.begin(), cookie->ports.end(), port) !=
                                      cookie->ports.end();
                        if (cookie->secure && !secure)
                            include = false;
                        if (include)
                            result.push_back(cookie);
                        ++i;
                    }
                }
                if (paths->paths.empty())
                    domains.remove(name);
            }
        };
        merge(primary, false);
        merge(plain, true);
        return result;
    }
};
namespace {
void validate_backend(const std::shared_ptr<Backend>& b) {
    if (!b || !b->now || !b->to_local_time || !b->parse_expiry || !b->try_int || !b->white_space ||
        !b->ordinal_ignore_case_equal || !b->lower_invariant || !b->fqdn_my_domain || !b->domain_hash)
        throw std::invalid_argument(
            "Cookie jar requires source clock/date/numeric/casing/FQDN/Hashtable hash providers");
}
}
ConfigHttpCookies::ConfigHttpCookies(std::shared_ptr<Backend> b)
    : ConfigHttpCookies(std::move(b), 300, 20, 4096) {
}
ConfigHttpCookies::ConfigHttpCookies(std::shared_ptr<Backend> b, int capacity, int per_domain, int size) {
    validate_backend(b);
    if (capacity <= 0)
        throw ConfigHttpCookieError(
            "System.ArgumentOutOfRangeException",
            u"capacity ('" + number(capacity) +
                u"') must be a non-negative and non-zero value. (Parameter 'capacity')" + newline() +
                u"Actual value was " + number(capacity) + u".",
            0x80131502, u"capacity");
    if (per_domain != std::numeric_limits<int>::max() && (per_domain <= 0 || per_domain > capacity))
        throw ConfigHttpCookieError("System.ArgumentOutOfRangeException",
                                    u"'PerDomainCapacity' has to be greater than '0' and less than '" +
                                        number(capacity) + u"'. (Parameter 'perDomainCapacity')",
                                    0x80131502, u"perDomainCapacity");
    if (size <= 0)
        throw ConfigHttpCookieError(
            "System.ArgumentOutOfRangeException",
            u"maxCookieSize ('" + number(size) +
                u"') must be a non-negative and non-zero value. (Parameter 'maxCookieSize')" + newline() +
                u"Actual value was " + number(size) + u".",
            0x80131502, u"maxCookieSize");
    state_ = std::make_shared<State>(std::move(b), capacity, per_domain, size);
}
void ConfigHttpCookies::set_cookies_for_uri(std::shared_ptr<ConfigCookieUri> uri, Text header) {
    if (!uri)
        null(u"uri");
    if (!header)
        null(u"cookieHeader");
    std::lock_guard lock(state_->mutex);
    std::vector<std::shared_ptr<Cookie>> parsed;
    try {
        Parser parser(header, *state_->backend);
        for (;;) {
            auto cookie = parser.get();
            if (!cookie) {
                if (parser.eof())
                    break;
            } else if (cookie->name->empty())
                cookie_error(u"Cookie format error.");
            else if (state_->verify(*cookie, uri))
                add_collection(parsed, std::move(cookie), *state_->backend);
        }
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        cookie_error(u"An error occurred when parsing the Cookie header for Uri '" + *uri->absolute_uri() +
                         u"'.",
                     std::current_exception());
    }
    for (auto& cookie : parsed)
        state_->add(cookie);
}
void ConfigHttpCookies::receive_for_uri(std::shared_ptr<ConfigCookieUri> uri,
                                        const std::vector<Text>& headers) {
    for (auto& header : headers)
        try {
            set_cookies_for_uri(uri, header);
        } catch (const ConfigHttpCookieError& e) {
            if (e.source_type != "System.Net.CookieException")
                throw;
        }
}
Text ConfigHttpCookies::header_for_uri(std::shared_ptr<ConfigCookieUri> uri) {
    if (!uri)
        null(u"uri");
    std::lock_guard lock(state_->mutex);
    auto cookies = state_->get(uri);
    std::u16string result;
    bool first = true;
    for (auto& cookie : cookies) {
        if (!first)
            result += u"; ";
        first = false;
        result += render(*cookie);
    }
    return t(std::move(result));
}
std::vector<ConfigHttpCookieView> ConfigHttpCookies::cookies_for_uri(std::shared_ptr<ConfigCookieUri> uri) {
    if (!uri)
        null(u"uri");
    std::lock_guard lock(state_->mutex);
    std::vector<ConfigHttpCookieView> result;
    for (auto& cookie : state_->get(uri))
        result.push_back(*cookie);
    return result;
}
std::int32_t ConfigHttpCookies::count() const {
    std::lock_guard lock(state_->mutex);
    return state_->count;
}
void ConfigHttpCookies::set_cookies(std::shared_ptr<ConfigUriComponents> uri, Text value) {
    set_cookies_for_uri(config_cookie_uri_components(std::move(uri)), std::move(value));
}
void ConfigHttpCookies::receive(std::shared_ptr<ConfigUriComponents> uri, const std::vector<Text>& values) {
    receive_for_uri(config_cookie_uri_components(std::move(uri)), values);
}
Text ConfigHttpCookies::header(std::shared_ptr<ConfigUriComponents> uri) {
    return header_for_uri(config_cookie_uri_components(std::move(uri)));
}
std::vector<ConfigHttpCookieView> ConfigHttpCookies::cookies(std::shared_ptr<ConfigUriComponents> uri) {
    return cookies_for_uri(config_cookie_uri_components(std::move(uri)));
}
}

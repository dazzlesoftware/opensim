// Source Marvin, HostInformationPal and invariant cookie parsing, .NET8.0.31.
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/file_system_name.hpp"
#include "opensim/framework/invariant_config_culture.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/text.hpp"
#include <array>
#include <bit>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#include <cerrno>
#if !defined(__APPLE__)
#include <sys/random.h>
#else
#include <cstdlib>
#endif
#endif
namespace opensim::framework {
namespace {
using Text = ConfigCookieText;
Text text(std::u16string s) {
    return std::make_shared<const std::u16string>(std::move(s));
}
void block(std::uint32_t& p0, std::uint32_t& p1) {
    p1 ^= p0;
    p0 = std::rotl(p0, 20);
    p0 += p1;
    p1 = std::rotl(p1, 9);
    p1 ^= p0;
    p0 = std::rotl(p0, 27);
    p0 += p1;
    p1 = std::rotl(p1, 19);
}
bool whitespace(char16_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f ||
           c == 0x3000;
}
#ifdef _WIN32
std::u16string win_message(DWORD code) {
    wchar_t* buffer = nullptr;
    auto n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::u16string result;
    if (n) {
        result.assign(reinterpret_cast<char16_t*>(buffer), n);
        LocalFree(buffer);
        while (!result.empty() && (result.back() == u'\r' || result.back() == u'\n' || result.back() == u' '))
            result.pop_back();
    } else {
        auto s = std::to_string(code);
        result = u"Unknown error (0x" + std::u16string(s.begin(), s.end()) + u")";
    }
    return result;
}
Text domain() {
    ULONG size = 0;
    DWORD result = GetNetworkParams(nullptr, &size);
    while (result == ERROR_BUFFER_OVERFLOW) {
        std::vector<std::uint8_t> bytes(size);
        result = GetNetworkParams(reinterpret_cast<FIXED_INFO*>(bytes.data()), &size);
        if (result == ERROR_SUCCESS) {
            auto info = reinterpret_cast<FIXED_INFO*>(bytes.data());
            auto length = static_cast<int>(std::char_traits<char>::length(info->DomainName));
            auto count = MultiByteToWideChar(CP_ACP, 0, info->DomainName, length, nullptr, 0);
            std::u16string value(count, u'\0');
            if (count)
                MultiByteToWideChar(CP_ACP, 0, info->DomainName, length,
                                    reinterpret_cast<wchar_t*>(value.data()), count);
            return text(std::move(value));
        }
    }
    if (result != ERROR_SUCCESS)
        throw ConfigHttpCookieError("System.ComponentModel.Win32Exception", win_message(result), 0x80004005);
    return text(u"");
}
#else
Text domain() {
    std::array<char, 256> bytes{};
    int result = getdomainname(bytes.data(), bytes.size());
    if (result != 0) {
        auto value = std::to_string(result);
        throw ConfigHttpCookieError("System.InvalidOperationException",
                                    u"GetDomainName: " + std::u16string(value.begin(), value.end()),
                                    0x80131509);
    }
    return text(
        utf16_replacing(std::string_view(bytes.data(), std::char_traits<char>::length(bytes.data()))));
}
#endif
}
std::uint32_t config_http_cookie_marvin_hash(std::u16string_view input, std::uint64_t seed) {
    std::uint32_t p0 = static_cast<std::uint32_t>(seed), p1 = static_cast<std::uint32_t>(seed >> 32);
    std::size_t i = 0;
    for (; i + 1 < input.size(); i += 2) {
        p0 += std::uint32_t(input[i]) | (std::uint32_t(input[i + 1]) << 16);
        block(p0, p1);
    }
    p0 += i < input.size() ? std::uint32_t(input[i]) | 0x800000u : 0x80u;
    block(p0, p1);
    block(p0, p1);
    return p0 ^ p1;
}
std::uint64_t native_config_http_cookie_hash_seed() {
    static const auto seed = []() {
        std::uint64_t value{};
#ifdef _WIN32
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            throw std::runtime_error("Native cookie hash entropy initialization failed");
#elif defined(__APPLE__)
        arc4random_buf(&value, sizeof(value));
#else
        auto pointer = reinterpret_cast<std::uint8_t*>(&value);
        std::size_t size = 0;
        while (size < sizeof(value)) {
            auto n = getrandom(pointer + size, sizeof(value) - size, 0);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                throw std::runtime_error("Native cookie hash entropy initialization failed");
            size += n;
        }
#endif
        return value;
    }();
    return seed;
}
Text native_config_http_cookie_fqdn_domain() {
    struct Cached {
        Text value;
        std::exception_ptr error;
    };
    static const auto cached = []() {
        Cached out;
        try {
            auto value = domain();
            out.value = text(value->size() <= 1 ? u"" : u"." + *value);
        } catch (...) {
            out.error = std::make_exception_ptr(ConfigHttpCookieError(
                "System.TypeInitializationException",
                u"The type initializer for 'System.Net.CookieContainer' threw an exception.", 0x80131534, {},
                std::current_exception()));
        }
        return out;
    }();
    if (cached.error)
        std::rethrow_exception(cached.error);
    return cached.value;
}
std::shared_ptr<ConfigHttpCookiesBackend>
native_invariant_config_http_cookies_backend(ConfigHttpCookieNativeOptions options) {
    auto context = options.context ? options.context : shared_native_invariant_datetime_context();
    auto b = std::make_shared<ConfigHttpCookiesBackend>();
    b->now = [context]() { return context->now(); };
    b->to_local_time = [context](auto date) { return context->to_local_time(date); };
    b->parse_expiry = [context](Text value) -> std::optional<AssetDateTime> {
        AssetDateTime out;
        if (!try_parse_invariant_datetime_cookie_expiry(
                value ? std::optional<std::u16string_view>(*value) : std::nullopt, out, context->callbacks()))
            return {};
        return out;
    };
    b->try_int =
        options.current_int_parser
            ? options.current_int_parser
            : std::function<std::optional<std::int32_t>(Text)>([](Text value) -> std::optional<std::int32_t> {
                  if (!value)
                      return {};
                  try {
                      return invariant_convert_to_int32(*value);
                  } catch (const InvariantNumberError&) {
                      return {};
                  }
              });
    b->white_space = whitespace;
    if (context->callbacks().globalization_invariant_mode &&
        (!options.ordinal_equal || !options.lower_invariant))
        throw std::invalid_argument("Cookie globalization-invariant mode requires explicit casing providers");
    b->ordinal_ignore_case_equal =
        options.ordinal_equal
            ? options.ordinal_equal
            : std::function<bool(std::u16string_view, std::u16string_view)>([](auto a, auto c) {
                  return a.size() == c.size() &&
                         file_system_name::pinned_case_backend().ordinal_ignore_case_ends_with(a, c);
              });
    b->lower_invariant = options.lower_invariant ? options.lower_invariant : invariant_config_lower;
    b->fqdn_my_domain =
        options.fqdn_snapshot ? *options.fqdn_snapshot : native_config_http_cookie_fqdn_domain();
    if (options.domain_hash)
        b->domain_hash = options.domain_hash;
    else if (options.hash_seed) {
        auto seed = *options.hash_seed;
        b->domain_hash = [seed](Text value) { return config_http_cookie_marvin_hash(*value, seed); };
    } else
        b->domain_hash = [](Text value) {
            return config_http_cookie_marvin_hash(*value, native_config_http_cookie_hash_seed());
        };
    return b;
}
}

#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/text.hpp"
#include <system_error>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <memory>
#else
#include <netdb.h>
#include <cerrno>
#endif
namespace opensim::framework {
namespace {
struct DnsErrorCodes {
    int native, socket;
};
DnsErrorCodes dns_error_codes(int resolver_error) {
#ifdef _WIN32
    switch (resolver_error) {
    case WSAHOST_NOT_FOUND:
        return {WSAHOST_NOT_FOUND, 11001};
    case WSATRY_AGAIN:
        return {WSATRY_AGAIN, 11002};
    case WSANO_RECOVERY:
        return {WSANO_RECOVERY, 11003};
    case WSANO_DATA:
        return {WSANO_DATA, 11004};
    }
#elif defined(__linux__)
    // Consumed .NET 8 HTTP asynchronous resolver behavior, verified by injecting
    // these original libc getaddrinfo results into both complete HTTP paths.
    switch (resolver_error) {
    case EAI_NONAME:
        return {-131073, 11001};
#ifdef EAI_NODATA
    case EAI_NODATA:
        return {-131073, 11001};
#endif
    case EAI_AGAIN:
        return {EAGAIN, 11002};
    case EAI_FAIL:
        return {11003, 11003};
    case EAI_BADFLAGS:
        return {EINVAL, 10022};
    case EAI_FAMILY:
        return {EAFNOSUPPORT, 10047};
#ifdef EAI_ADDRFAMILY
    case EAI_ADDRFAMILY:
        return {-131074, -1};
#endif
    case EAI_SERVICE:
    case EAI_MEMORY:
    case EAI_SYSTEM:
        return {-131074, -1};
    }
#else
    (void)resolver_error;
    throw std::logic_error("Exact DNS failure metadata has not been verified on this platform");
#endif
    throw std::logic_error("Exact DNS failure provider is required for this resolver code");
}
std::u16string dns_error_message(DnsErrorCodes codes) {
#ifdef _WIN32
    wchar_t* raw = nullptr;
    auto count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, codes.native, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    std::unique_ptr<wchar_t, decltype(&LocalFree)> buffer(raw, &LocalFree);
    if (!count)
        throw std::system_error(GetLastError(), std::system_category(), "DNS socket message lookup");
    std::u16string message(reinterpret_cast<const char16_t*>(buffer.get()), count);
    while (!message.empty() && message.back() <= u' ')
        message.pop_back();
    return message;
#elif defined(__linux__)
    // The two negative values are runtime PAL socket errors rather than errno.
    if (codes.native == -131073)
        return utf16_replacing(gai_strerror(EAI_NONAME));
    if (codes.native == -131074)
        return u"Unknown socket error";
    return utf16_replacing(std::system_category().message(codes.native));
#else
    (void)codes;
    throw std::logic_error("Exact DNS failure metadata has not been verified on this platform");
#endif
}
}
std::exception_ptr config_http_dns_error(std::string host, int port, int resolver_error) {
    auto codes = dns_error_codes(resolver_error);
    auto message = dns_error_message(codes);
    auto cause = std::make_exception_ptr(ConfigHttpSocketError(message, codes.native, codes.socket));
    if (host.find(':') != host.npos && !host.starts_with('['))
        host = '[' + host + ']';
    ConfigHttpConnectionError error(
        message + u" (" + utf16_replacing(host + ":" + std::to_string(port)) + u")", std::move(cause));
    error.request_error = (codes.socket == 11001 || codes.socket == 11002) ? 1 : 2;
    return std::make_exception_ptr(error);
}
}

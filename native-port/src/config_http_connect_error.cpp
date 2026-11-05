#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/text.hpp"
#include <system_error>
#include <memory>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <cerrno>
#endif
namespace opensim::framework {
namespace {
int socket_error_code(int code) {
#ifdef _WIN32
    switch (code) {
    case WSAEACCES:
        return 10013;
    case WSAEADDRNOTAVAIL:
        return 10049;
    case WSAENETDOWN:
        return 10050;
    case WSAENETUNREACH:
        return 10051;
    case WSAECONNABORTED:
        return 10053;
    case WSAECONNRESET:
        return 10054;
    case WSAESHUTDOWN:
        return 10058;
    case WSAETIMEDOUT:
        return 10060;
    case WSAECONNREFUSED:
        return 10061;
    case WSAEHOSTUNREACH:
        return 10065;
    }
#else
    switch (code) {
    case EACCES:
        return 10013;
    case EADDRNOTAVAIL:
        return 10049;
    case ENETDOWN:
        return 10050;
    case ENETUNREACH:
        return 10051;
    case ECONNABORTED:
        return 10053;
    case ECONNRESET:
        return 10054;
    case EPIPE:
        return 10058;
    case ETIMEDOUT:
        return 10060;
    case ECONNREFUSED:
        return 10061;
    case EHOSTUNREACH:
        return 10065;
    }
#endif
    throw std::logic_error("Exact native connection-error provider is required for this socket code");
}
}
ConfigHttpSocketError config_http_socket_error(int native_error) {
    const int socket_error = socket_error_code(native_error);
#ifdef _WIN32
    wchar_t* raw = nullptr;
    auto count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, native_error, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    std::unique_ptr<wchar_t, decltype(&LocalFree)> buffer(raw, &LocalFree);
    if (!count) {
        auto failure_code = GetLastError();
        throw std::system_error(failure_code, std::system_category(), "Socket message lookup");
    }
    std::u16string message(reinterpret_cast<const char16_t*>(buffer.get()), count);
    while (!message.empty() && message.back() <= u' ')
        message.pop_back();
#else
    auto message = utf16_replacing(std::system_category().message(native_error));
#endif
    return ConfigHttpSocketError(std::move(message), native_error, socket_error);
}
std::exception_ptr config_http_connect_error(std::string host, int port, int native_error) {
    auto socket = config_http_socket_error(native_error);
    auto message = socket.message;
    auto cause = std::make_exception_ptr(std::move(socket));
    if (host.find(':') != host.npos && !host.starts_with('['))
        host = '[' + host + ']';
    auto endpoint = host + ":" + std::to_string(port);
    return std::make_exception_ptr(
        ConfigHttpConnectionError(message + u" (" + utf16_replacing(endpoint) + u")", std::move(cause)));
}
}

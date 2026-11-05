#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#endif
using namespace opensim::framework;
std::u16string decode(std::string s) {
    std::u16string t;
    for (std::size_t i = 0; i < s.size(); i += 4)
        t += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return t;
}
std::string hex(std::u16string_view s) {
    std::string t;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            t += "0123456789abcdef"[(c >> n) & 15];
    return t;
}
int native_code(int managed) {
#ifdef _WIN32
    return managed;
#else
    switch (managed) {
    case 10013:
        return EACCES;
    case 10049:
        return EADDRNOTAVAIL;
    case 10050:
        return ENETDOWN;
    case 10051:
        return ENETUNREACH;
    case 10053:
        return ECONNABORTED;
    case 10054:
        return ECONNRESET;
    case 10058:
        return EPIPE;
    case 10060:
        return ETIMEDOUT;
    case 10061:
        return ECONNREFUSED;
    case 10065:
        return EHOSTUNREACH;
    default:
        throw std::logic_error("unmapped fixture code");
    }
#endif
}
int main(int argc, char**) {
    int code =
#ifdef _WIN32
        WSAECONNREFUSED;
#else
        ECONNREFUSED;
#endif
    if (argc == 1) {
        bool required = false;
        try {
            config_http_connect_error("localhost", 80, 0);
        } catch (const std::logic_error&) {
            required = true;
        }
        if (!required)
            return 1;
        try {
            std::rethrow_exception(config_http_connect_error("::1", 81, code));
        } catch (const ConfigHttpConnectionError& e) {
            if (!e.retry_on_next_proxy || e.request_error != 2 || e.hresult != 0x80004005)
                return 2;
            try {
                std::rethrow_exception(e.inner);
            } catch (const ConfigHttpSocketError& s) {
                if (s.native_error != code || s.socket_error != 10061)
                    return 3;
                return 0;
            }
        }
        return 4;
    }
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        std::string host;
        int port, managed = 10061;
        in >> host >> port;
        if (in >> managed) {
        };
        try {
            std::rethrow_exception(config_http_connect_error(utf8(decode(host)), port, native_code(managed)));
        } catch (const ConfigHttpConnectionError& e) {
            std::cout << e.source_type << ":" << static_cast<std::int32_t>(e.hresult) << ":"
                      << e.request_error << ":" << (e.retry_on_next_proxy ? "RetryOnNextProxy" : "NoRetry")
                      << ":" << hex(e.message);
            try {
                std::rethrow_exception(e.inner);
            } catch (const ConfigHttpSocketError& s) {
                std::cout << "|" << s.source_type << ":" << static_cast<std::int32_t>(s.hresult) << ":"
                          << s.native_error << ":" << s.socket_error << ":" << hex(s.message);
            }
            std::cout << "\n";
        }
    }
}

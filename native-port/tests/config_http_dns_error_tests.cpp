#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netdb.h>
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
int resolver_code(const std::string& name) {
#ifdef _WIN32
    if (name == "noname")
        return WSAHOST_NOT_FOUND;
    if (name == "again")
        return WSATRY_AGAIN;
    if (name == "fail")
        return WSANO_RECOVERY;
    if (name == "nodata")
        return WSANO_DATA;
#elif defined(__linux__)
    if (name == "noname")
        return EAI_NONAME;
    if (name == "again")
        return EAI_AGAIN;
    if (name == "fail")
        return EAI_FAIL;
    if (name == "nodata")
        return EAI_NODATA;
    if (name == "addrfamily")
        return EAI_ADDRFAMILY;
    if (name == "family")
        return EAI_FAMILY;
    if (name == "badflags")
        return EAI_BADFLAGS;
    if (name == "service")
        return EAI_SERVICE;
    if (name == "memory")
        return EAI_MEMORY;
    if (name == "system")
        return EAI_SYSTEM;
#endif
    throw std::logic_error("unmapped DNS fixture code");
}
int main(int argc, char** argv) {
#if !defined(_WIN32) && !defined(__linux__)
    // The provider must keep its unverified platform dependency explicit.
    try {
        config_http_dns_error("localhost", 80, EAI_NONAME);
    } catch (const std::logic_error&) {
        return 0;
    }
    return 1;
#else
    int code =
#ifdef _WIN32
        WSAHOST_NOT_FOUND;
#else
        EAI_NONAME;
#endif
    int native_code =
#ifdef _WIN32
        WSAHOST_NOT_FOUND;
#else
        -131073;
#endif
    if (argc == 1) {
        bool required = false;
        try {
            config_http_dns_error("localhost", 80, 0);
        } catch (const std::logic_error&) {
            required = true;
        }
        if (!required)
            return 1;
        try {
            std::rethrow_exception(config_http_dns_error("::1", 81, code));
        } catch (const ConfigHttpConnectionError& e) {
            if (!e.retry_on_next_proxy || e.request_error != 1 || e.hresult != 0x80004005)
                return 2;
            try {
                std::rethrow_exception(e.inner);
            } catch (const ConfigHttpSocketError& s) {
                if (s.native_error != native_code || s.socket_error != 11001)
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
        int port;
        std::string outcome = "noname";
        in >> host >> port;
        if (in >> outcome) {
        };
        try {
            std::rethrow_exception(config_http_dns_error(utf8(decode(host)), port, [&] {
#if defined(__linux__)
                if (std::string_view(argv[1]) == "--resolve") {
                    addrinfo hints{}, *addresses = nullptr;
                    hints.ai_socktype = SOCK_STREAM;
                    int result = getaddrinfo(utf8(decode(host)).c_str(), std::to_string(port).c_str(), &hints,
                                             &addresses);
                    if (addresses)
                        freeaddrinfo(addresses);
                    if (!result)
                        throw std::logic_error("DNS fault fixture unexpectedly resolved a host");
                    return result;
                }
#endif
                return resolver_code(outcome);
            }()));
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
#endif
}

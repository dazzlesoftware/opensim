#include "opensim/framework/config_http_windows_proxy_bypass.hpp"
#include "opensim/framework/config_uri_components.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
namespace opensim::framework {
std::vector<IpAddress> native_config_http_windows_local_addresses() {
    ULONG size = 0;
    constexpr ULONG flags = GAA_FLAG_INCLUDE_WINS_INFO | GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG code = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &size);
    std::vector<unsigned char> buffer;
    std::vector<IpAddress> result;
    while (code == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        auto adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        code = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
        if (code == NO_ERROR)
            for (auto p = adapters; p; p = p->Next)
                for (auto address = p->FirstUnicastAddress; address; address = address->Next) {
                    auto sa = address->Address.lpSockaddr;
                    std::array<std::uint8_t, 16> bytes{};
                    if (sa->sa_family == AF_INET) {
                        auto v4 = reinterpret_cast<const sockaddr_in*>(sa);
                        CopyMemory(bytes.data(), &v4->sin_addr, 4);
                        result.push_back(IpAddress::from_bytes(bytes, false));
                    } else if (sa->sa_family == AF_INET6) {
                        auto v6 = reinterpret_cast<const sockaddr_in6*>(sa);
                        CopyMemory(bytes.data(), &v6->sin6_addr, 16);
                        result.push_back(IpAddress::from_bytes(bytes, true, v6->sin6_scope_id));
                    }
                }
    }
    if (code == NO_ERROR || code == ERROR_INVALID_PARAMETER || code == ERROR_NO_DATA)
        return result;
    wchar_t* text = nullptr;
    auto count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    if (!count)
        throw ConfigHttpProxyProviderError(
            "NetworkInformationException requires native error message provider");
    struct Release {
        wchar_t* value;
        ~Release() {
            LocalFree(value);
        }
    } release{text};
    std::u16string message(reinterpret_cast<const char16_t*>(text), count);
    while (!message.empty() && (message.back() == u'\r' || message.back() == u'\n' || message.back() == u' '))
        message.pop_back();
    throw ConfigUriComponentsError("System.Net.NetworkInformation.NetworkInformationException",
                                   std::move(message), 0x80004005);
}
}
#else
namespace opensim::framework {
std::vector<IpAddress> native_config_http_windows_local_addresses() {
    throw ConfigHttpProxyProviderError("Windows interface enumeration requires its OS provider");
}
}
#endif

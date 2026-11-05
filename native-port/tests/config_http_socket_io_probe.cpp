#include "opensim/framework/config_http_write_error.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#endif
using namespace opensim::framework;
namespace {
std::string hex(std::u16string_view text) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : text)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
void dump(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const ConfigHttpReadError& e) {
        std::cout << e.source_type << ';' << hex(e.message) << ';' << e.hresult << ';' << e.request_error
                  << ";-1;-1|";
        dump(e.inner);
    } catch (const ConfigHttpSocketError& e) {
        std::cout << e.source_type << ';' << hex(e.message) << ';' << e.hresult << ";-1;" << e.native_error
                  << ';' << e.socket_error;
    }
}
}
int main() {
    const int native[] = {
#ifdef _WIN32
        WSAEACCES,    WSAEADDRNOTAVAIL, WSAENETDOWN,     WSAENETUNREACH, WSAECONNABORTED, WSAECONNRESET,
        WSAESHUTDOWN, WSAETIMEDOUT,     WSAECONNREFUSED, WSAEHOSTUNREACH
#else
        EACCES, EADDRNOTAVAIL, ENETDOWN,     ENETUNREACH, ECONNABORTED, ECONNRESET,
        EPIPE,  ETIMEDOUT,     ECONNREFUSED, EHOSTUNREACH
#endif
    };
    int index, write, headers;
    while (std::cin >> index >> write >> headers) {
        if (index < 0 || index >= 10)
            return 2;
        dump(write ? config_http_write_error(native[index])
                   : config_http_read_error(headers != 0, native[index]));
        std::cout << '\n';
    }
}

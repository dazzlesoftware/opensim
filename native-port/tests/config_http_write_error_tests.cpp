#include "opensim/framework/config_http_write_error.hpp"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#endif
using namespace opensim::framework;
int main() {
    bool rejected = false;
    try {
        config_http_write_error(0);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    if (!rejected)
        return 1;
    int codes[] = {
#ifdef _WIN32
        WSAECONNRESET, WSAESHUTDOWN
#else
        ECONNRESET, EPIPE
#endif
    };
    for (int i = 0; i < 2; ++i) {
        try {
            std::rethrow_exception(config_http_write_error(codes[i]));
        } catch (const ConfigHttpReadError& request) {
            if (request.source_type != "System.Net.Http.HttpRequestException" || request.request_error != 0 ||
                request.hresult != 0x80131620)
                return 2;
            try {
                std::rethrow_exception(request.inner);
            } catch (const ConfigHttpReadError& io) {
                if (io.source_type != "System.IO.IOException" || io.request_error != -1 ||
                    io.hresult != 0x80131620 ||
                    !io.message.starts_with(u"Unable to write data to the transport connection: "))
                    return 3;
                try {
                    std::rethrow_exception(io.inner);
                } catch (const ConfigHttpSocketError& socket) {
                    if (socket.native_error != codes[i] || socket.socket_error != (i ? 10058 : 10054) ||
                        socket.hresult != 0x80004005)
                        return 4;
                }
            }
        }
    }
}

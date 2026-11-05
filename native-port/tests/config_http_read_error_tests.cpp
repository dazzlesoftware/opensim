#include "opensim/framework/config_http_read_error.hpp"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#endif
using namespace opensim::framework;
int main() {
    bool rejected = false;
    try {
        config_http_read_error(true, 0);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    if (!rejected)
        return 1;
    int code =
#ifdef _WIN32
        WSAECONNRESET;
#else
        ECONNRESET;
#endif
    for (bool headers : {false, true}) {
        try {
            std::rethrow_exception(config_http_read_error(headers, code));
        } catch (const ConfigHttpReadError& error) {
            if (error.hresult != 0x80131620 || error.request_error != (headers ? 0 : -1))
                return 2;
            const ConfigHttpReadError* io = &error;
            if (headers) {
                if (error.source_type != "System.Net.Http.HttpRequestException")
                    return 3;
                try {
                    std::rethrow_exception(error.inner);
                } catch (const ConfigHttpReadError& inner) {
                    if (inner.source_type != "System.IO.IOException" || inner.request_error != -1)
                        return 4;
                    try {
                        std::rethrow_exception(inner.inner);
                    } catch (const ConfigHttpSocketError& s) {
                        if (s.native_error != code || s.socket_error != 10054 || s.hresult != 0x80004005)
                            return 5;
                    }
                }
            } else {
                if (io->source_type != "System.IO.IOException")
                    return 6;
                try {
                    std::rethrow_exception(io->inner);
                } catch (const ConfigHttpSocketError& s) {
                    if (s.native_error != code || s.socket_error != 10054)
                        return 7;
                }
            }
        }
    }
}

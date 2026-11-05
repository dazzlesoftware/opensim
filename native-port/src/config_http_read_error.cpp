#include "opensim/framework/config_http_read_error.hpp"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#endif
namespace opensim::framework {
std::exception_ptr config_http_read_error(bool reading_headers, int native_error) {
    // NetworkStream.WrapException applies the same I/O wrapper to every socket
    // failure. The shared socket adapter still rejects unverified native codes.
    auto socket = config_http_socket_error(native_error);
    auto message = u"Unable to read data from the transport connection: " + socket.message + u".";
    auto error = std::make_exception_ptr(ConfigHttpReadError("System.IO.IOException", std::move(message), -1,
                                                             std::make_exception_ptr(std::move(socket))));
    if (reading_headers)
        return std::make_exception_ptr(ConfigHttpReadError("System.Net.Http.HttpRequestException",
                                                           u"An error occurred while sending the request.", 0,
                                                           std::move(error)));
    return error;
}
}

#include "opensim/framework/config_http_write_error.hpp"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#endif
namespace opensim::framework {
std::exception_ptr config_http_write_error(int native_error) {
    // NetworkStream.WrapException is independent of SocketErrorCode; retain the
    // exact shared socket adapter's supported-code boundary.
    auto socket = config_http_socket_error(native_error);
    auto message = u"Unable to write data to the transport connection: " + socket.message + u".";
    auto io = std::make_exception_ptr(ConfigHttpReadError("System.IO.IOException", std::move(message), -1,
                                                          std::make_exception_ptr(std::move(socket))));
    return std::make_exception_ptr(ConfigHttpReadError("System.Net.Http.HttpRequestException",
                                                       u"An error occurred while sending the request.", 0,
                                                       std::move(io)));
}
}

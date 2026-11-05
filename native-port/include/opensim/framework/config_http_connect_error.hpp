// Structured consumed HttpRequestException/SocketException connection failures.
#pragma once
#include "opensim/framework/config_uri_components.hpp"
#include <exception>
namespace opensim::framework {
class ConfigHttpSocketError : public ConfigUriComponentsError {
  public:
    ConfigHttpSocketError(std::u16string message, int native_code, int socket_code)
        : ConfigUriComponentsError("System.Net.Sockets.SocketException", std::move(message), 0x80004005),
          native_error(native_code), socket_error(socket_code) {}
    int native_error, socket_error;
};
class ConfigHttpConnectionError : public ConfigUriComponentsError {
  public:
    ConfigHttpConnectionError(std::u16string message, std::exception_ptr cause)
        : ConfigUriComponentsError("System.Net.Http.HttpRequestException", std::move(message), 0x80004005),
          inner(std::move(cause)) {}
    // ConnectHelper.CreateWrappedException permits advancing to the next proxy.
    bool retry_on_next_proxy = true;
    int request_error = 2;
    std::exception_ptr inner;
};
// Constructs the source SocketException for a verified native socket code.
ConfigHttpSocketError config_http_socket_error(int native_error);
// Translates verified native socket connect failures. Unmapped OS failures remain
// explicit dependencies; DNS resolver results use the distinct DNS provider.
std::exception_ptr config_http_connect_error(std::string host, int port, int native_error);
}

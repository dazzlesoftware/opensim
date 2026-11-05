// Consumed NetworkStream read failures and HttpConnection header wrapping.
#pragma once
#include "opensim/framework/config_http_connect_error.hpp"
namespace opensim::framework {
class ConfigHttpReadError : public ConfigUriComponentsError {
  public:
    ConfigHttpReadError(std::string type, std::u16string message, int error, std::exception_ptr cause)
        : ConfigUriComponentsError(std::move(type), std::move(message), 0x80131620), request_error(error),
          inner(std::move(cause)) {}
    int request_error;
    std::exception_ptr inner;
};
// A socket failure during headers wraps IOException in HttpRequestException(Unknown),
// without retry. During the body it surfaces IOException directly.
// Uses the shared verified native socket-code mapping; cancellation/timeouts
// require their own contextual handling rather than this non-cancelled path.
std::exception_ptr config_http_read_error(bool reading_headers, int native_error);
}

// Consumed non-cancelled HTTP request send failures.
#pragma once
#include "opensim/framework/config_http_read_error.hpp"
namespace opensim::framework {
// Uses the common structured I/O exception representation. Verified socket codes
// map to HttpRequestException(Unknown, NoRetry) -> IOException -> SocketException.
// Unobserved socket errors and cancellation remain distinct dependencies.
std::exception_ptr config_http_write_error(int native_error);
}

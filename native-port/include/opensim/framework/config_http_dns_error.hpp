// Native resolver failures consumed by the XML HTTP configuration path.
#pragma once
#include "opensim/framework/config_http_connect_error.hpp"
namespace opensim::framework {
// resolver_error is the original getaddrinfo result, not errno/WSAGetLastError.
// Preserves distinct Windows/Linux DNS outcomes and HTTP error classification.
// Unmapped resolver codes and unverified platforms remain explicit dependencies.
std::exception_ptr config_http_dns_error(std::string host,int port,int resolver_error);
}

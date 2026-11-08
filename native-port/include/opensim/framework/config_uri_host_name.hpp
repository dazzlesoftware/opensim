// Source .NET8.0.31 Uri.CheckHostName; MIT .NET license.
#pragma once
#include "opensim/framework/config_uri.hpp"
namespace opensim::framework {
enum class ConfigUriHostNameType { Unknown = 0, Basic = 1, Dns = 2, IPv4 = 3, IPv6 = 4 };
ConfigUriHostNameType config_uri_check_host_name(ConfigUriText);
} // namespace opensim::framework

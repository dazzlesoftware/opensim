#pragma once
#include "opensim/framework/config_uri_object.hpp"
namespace opensim::framework {
// Source UriHostNameType values: Unknown0, Basic1, Dns2, IPv4=3, IPv6=4.
struct ConfigUriNetworkIdentity {
    int host_name_type = 0;
    bool loopback = false;
};
ConfigUriNetworkIdentity config_generic_uri_network_identity(ConfigUriObjectPtr);
// Consumed native built-in URI model; custom UriParser implementations need
// their own provider rather than reinterpretation as one of these native types.
ConfigUriNetworkIdentity native_config_uri_network_identity(ConfigUriObjectPtr);
}

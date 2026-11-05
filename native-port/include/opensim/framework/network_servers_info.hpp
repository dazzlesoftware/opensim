// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/config_settings.hpp"
#include "opensim/framework/nini_config.hpp"
namespace opensim::framework {
struct NetworkServersConfigAccess {
    std::function<std::int32_t(NiniWriterText, std::int32_t)> integer;
    std::function<bool(NiniWriterText, bool)> boolean;
    std::function<NiniWriterText(NiniWriterText, NiniWriterText)> text;
};
using NetworkServersConfigLookup = std::function<std::shared_ptr<NetworkServersConfigAccess>(NiniWriterText)>;
NiniWriterText network_servers_empty_text();
struct NetworkServersInfo {
    std::uint32_t HttpListenerPort = ConfigSettings::DefaultRegionHttpPort;
    bool secureInventoryServer = false, isSandbox = false, HttpUsesSSL = false;
    NiniWriterText HttpSSLCN = network_servers_empty_text(), HttpSSLCertPath = network_servers_empty_text(),
                   HttpSSLCNCertPass = network_servers_empty_text();
    std::uint32_t httpSSLPort = 9001;
    bool ssl_listener = false, ssl_external = false;
    std::uint32_t https_port = 0;
    NiniWriterText cert_path = network_servers_empty_text(), cert_pass = network_servers_empty_text();
    NetworkServersInfo() = default;
    NetworkServersInfo(std::uint32_t, std::uint32_t) {}
    void load_from_configuration(const std::shared_ptr<NiniConfigSourceBase>&);
    void load_from_configuration(const NetworkServersConfigLookup&);
};
struct RegionConfigurationBackend;
// Binds the original loader's concrete create/load callbacks, retaining the
// resulting NetworkServersInfo in its opaque network-info slot.
void bind_network_servers_info(RegionConfigurationBackend&);
}

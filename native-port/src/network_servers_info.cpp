// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/network_servers_info.hpp"
#include "opensim/framework/configuration_loader.hpp"
#include <map>
namespace opensim::framework {
NiniWriterText network_servers_empty_text() {
    static const auto value = nini_writer_text(u"");
    return value;
}
namespace {
NiniWriterText literal(std::u16string_view text) {
    static const auto literals = []() {
        std::map<std::u16string_view, NiniWriterText> result;
        for (auto value :
             {u"Network", u"http_listener_port", u"http_listener_sslport", u"http_listener_ssl",
              u"http_listener_cn", u"http_listener_cert_path", u"http_listener_cert_pass", u"https_listener",
              u"https_external", u"cert_path", u"cert_pass", u"https_port", u"localhost"})
            result.emplace(value, nini_writer_text(std::u16string(value)));
        return result;
    }();
    return literals.at(text);
}
[[noreturn]] void null_reference() {
    throw NiniConfigError(NiniConfigError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
}
void NetworkServersInfo::load_from_configuration(const NetworkServersConfigLookup &lookup) {
    if (!lookup)
        null_reference();
    auto config = lookup(literal(u"Network"));
    if (!config)
        return;
    HttpListenerPort = static_cast<std::uint32_t>(config->integer(
        literal(u"http_listener_port"), static_cast<std::int32_t>(ConfigSettings::DefaultRegionHttpPort)));
    httpSSLPort = static_cast<std::uint32_t>(
        config->integer(literal(u"http_listener_sslport"),
                        static_cast<std::int32_t>(ConfigSettings::DefaultRegionHttpPort) + 1));
    HttpUsesSSL = config->boolean(literal(u"http_listener_ssl"), false);
    static const auto localhost = literal(u"localhost");
    HttpSSLCN = config->text(literal(u"http_listener_cn"), localhost);
    HttpSSLCertPath = config->text(literal(u"http_listener_cert_path"), HttpSSLCertPath);
    HttpSSLCNCertPass = config->text(literal(u"http_listener_cert_pass"), HttpSSLCNCertPass);
    ssl_listener = config->boolean(literal(u"https_listener"), false);
    ssl_external = config->boolean(literal(u"https_external"), false);
    if (ssl_listener) {
        cert_path = config->text(literal(u"cert_path"), network_servers_empty_text());
        cert_pass = config->text(literal(u"cert_pass"), network_servers_empty_text());
        https_port = static_cast<std::uint32_t>(config->integer(literal(u"https_port"), 0));
    }
}
void NetworkServersInfo::load_from_configuration(const std::shared_ptr<NiniConfigSourceBase> &source) {
    load_from_configuration(NetworkServersConfigLookup(
        [source](NiniWriterText name) -> std::shared_ptr<NetworkServersConfigAccess> {
            if (!source)
                null_reference();
            auto config = source->configs()->at(name);
            if (!config)
                return {};
            auto access = std::make_shared<NetworkServersConfigAccess>();
            access->integer = [config](auto key, auto fallback) { return config->get_int(key, fallback); };
            access->boolean = [config](auto key, auto fallback) {
                return config->get_boolean(key, fallback);
            };
            access->text = [config](auto key, auto fallback) {
                return config->get_string(key, std::move(fallback));
            };
            return access;
        }));
}
void bind_network_servers_info(RegionConfigurationBackend &backend) {
    backend.create_network_info = []() -> std::shared_ptr<void> {
        return std::make_shared<NetworkServersInfo>();
    };
    backend.load_network_info = [](std::shared_ptr<void> object,
                                   std::shared_ptr<NiniConfigSourceBase> source) {
        if (!object)
            null_reference();
        std::static_pointer_cast<NetworkServersInfo>(object)->load_from_configuration(source);
    };
}
}

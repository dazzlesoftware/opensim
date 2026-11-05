#include "opensim/server/config.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_read_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/nini_xml_resolver_policy.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
using namespace opensim::framework;
std::string hex(std::u16string_view s) {
    std::string r;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            r += "0123456789abcdef"[(c >> n) & 15];
    return r;
}
int main(int argc, char** argv) {
    bool defaults = false, disabled = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--default")
            defaults = true;
        if (std::string_view(argv[i]) == "--disabled")
            disabled = true;
    }
    for (std::string row; std::getline(std::cin, row);) {
        std::u16string cause;
        try {
            opensim::server::ConfigurationStartupOptions options;
            options.application = std::make_shared<RegionApplicationConfigurationState>();
            options.inidirectory = nini_writer_text(u"__startup_network_no_scan__");
            options.log = [&](RegionLoaderLogLevel level, NiniWriterText message, auto, auto) {
                auto prefix = u"[CONFIG]: Exception reading config from URI " + utf16(row) + u"\n ";
                if (level == RegionLoaderLogLevel::Fatal && message && message->starts_with(prefix))
                    cause = message->substr(prefix.size());
            };
            options.xml_http_services = std::make_shared<ConfigXmlNativeHttpServices>();
            auto& services = *options.xml_http_services;
            if (!defaults)
                services.proxy = [](auto, auto) -> std::optional<ConfigXmlHttpRoute> {
                    return {};
                };
            services.error = [](auto, int) {
                return std::make_exception_ptr(std::logic_error("Unmapped test HTTP failure"));
            };
            services.connect_error = config_http_connect_error;
            services.dns_error = config_http_dns_error;
            services.read_error = config_http_read_error;
            services.write_error = config_http_write_error;
            if (disabled)
                options.xml_resolver_policy = std::make_shared<NiniXmlResolverPolicy>(
                    [](std::u16string_view name) -> std::optional<bool> {
                        if (name == u"System.Xml.XmlResolver.IsNetworkingEnabledByDefault")
                            return false;
                        return {};
                    });
            auto config = opensim::server::IniConfig::load_startup(std::filesystem::path(utf16(row)),
                                                                   std::move(options));
            std::cout << hex(utf16(config.get("Startup", "Name"))) << ":"
                      << hex(utf16(config.get("Startup", "Greeting"))) << "\n";
        } catch (const opensim::server::ConfigurationStartupExit& e) {
            std::cerr << "EXIT:" << e.code << "\n" << e.what() << "\n";
            std::cout << "E:" << (cause.empty() ? e.what() : utf8(cause)) << "\n";
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            std::cout << "E:" << e.what() << "\n";
        }
    }
}

#include "opensim/server/config.hpp"
#include "opensim/server/config_xml_startup.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/nini_reader.hpp"
#include <chrono>
#include <fstream>
using namespace opensim::framework;
opensim::server::ConfigurationStartupOptions options() {
    opensim::server::ConfigurationStartupOptions o;
    o.application = std::make_shared<RegionApplicationConfigurationState>();
    o.inidirectory = nini_writer_text(u"__no_xml_startup_scan__");
    o.log = [](auto, auto, auto, auto) {};
    o.xml_http_services = std::make_shared<ConfigXmlNativeHttpServices>();
    return o;
}
int main() {
    auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
    bool rejected = false;
    try {
        opensim::server::native_startup_xml_source({}, {});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected)
        return 1;
    auto lazy = opensim::server::native_startup_xml_source(arena, {});
    rejected = false;
    try {
        lazy(nini_writer_text(u"http://127.0.0.1:1/unused"));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected)
        return 2;
    auto disabled =
        std::make_shared<NiniXmlResolverPolicy>([](std::u16string_view name) -> std::optional<bool> {
            if (name == u"System.Xml.XmlResolver.IsNetworkingEnabledByDefault")
                return false;
            return {};
        });
    auto prohibited = opensim::server::native_startup_xml_source(arena, {}, disabled);
    bool denied = false;
    try {
        prohibited(nini_writer_text(u"http://127.0.0.1:1/blocked"));
    } catch (const std::exception& e) {
        denied = std::string(e.what()).find("Object type is not supported.") != std::string::npos;
    }
    if (!denied)
        return 6;
    // The default proxy is initialized lazily within the startup URI adapter,
    // and networking denial must run before even this getter is reached.
    ConfigXmlNativeHttpServices services;
    int selected = 0;
    services.error = [](auto, int) {
        return std::make_exception_ptr(std::logic_error("Unexpected HTTP operation"));
    };
    services.default_proxy = [&]() -> std::shared_ptr<ConfigXmlProxy> {
        ++selected;
        throw std::runtime_error("Startup default proxy getter reached");
    };
    auto bound = opensim::server::native_startup_xml_source(arena, services);
    bool reached = false;
    try {
        bound(nini_writer_text(u"http://127.0.0.1:1/default"));
    } catch (const std::exception& e) {
        reached = std::string(e.what()).find("Startup default proxy getter reached") != std::string::npos;
    }
    if (!reached || selected != 1)
        return 7;
    auto blocked_default = opensim::server::native_startup_xml_source(arena, services, disabled);
    try {
        blocked_default(nini_writer_text(u"http://127.0.0.1:1/default-blocked"));
    } catch (const std::exception&) {
    }
    if (selected != 1)
        return 8;
    auto folder = std::filesystem::temp_directory_path() /
                  ("opensim-xml-startup-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(folder))
        return 3;
    auto file = folder / "local.ini";
    struct Cleanup {
        std::filesystem::path file, folder;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove(file, e);
            std::filesystem::remove(folder, e);
        }
    } cleanup{file, folder};
    {
        std::ofstream out(file);
        out << "[Startup]\nName = Local Startup\nGreeting = local\n";
    }
    auto local = opensim::server::IniConfig::load_startup(file, options());
    if (local.get("Startup", "Name") != "Local Startup")
        return 4;
    auto custom = options();
    int calls = 0;
    custom.xml_uri_source = [&](NiniWriterText) {
        ++calls;
        return arena->create_ini_source(nini_string_reader(u"[Startup]\nName = Custom XML\n"));
    };
    auto overridden = opensim::server::IniConfig::load_startup(
        std::filesystem::path(u"http://127.0.0.1:1/custom"), std::move(custom));
    if (calls != 1 || overridden.get("Startup", "Name") != "Custom XML")
        return 5;
}

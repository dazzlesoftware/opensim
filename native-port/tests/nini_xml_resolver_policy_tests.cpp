#include "opensim/framework/nini_xml_resolver_policy.hpp"
#include <iostream>
#include <map>
#include <string>
#include <stdexcept>
using namespace opensim::framework;
int main(int argc, char**) {
    int calls = 0;
    NiniXmlResolverPolicy defaults([&](std::u16string_view) -> std::optional<bool> {
        ++calls;
        return {};
    });
    bool enabled = defaults.networking_enabled_by_default();
    if (!enabled || defaults.cache_snapshot() != 1 || !defaults.networking_enabled_by_default() || calls != 2)
        throw std::runtime_error("XML default resolver cache regression");
    std::cout << "DEFAULT:" << (enabled ? 1 : 0) << "," << defaults.cache_snapshot() << ";\n";
    if (argc == 1)
        return 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::map<std::u16string, bool> switches;
        switches[u"System.Xml.XmlResolver.IsNetworkingEnabledByDefault"] = true;
        switches[u"TestSwitch.LocalAppContext.DisableCaching"] = false;
        NiniXmlResolverPolicy policy([&](std::u16string_view key) -> std::optional<bool> {
            auto found = switches.find(std::u16string(key));
            return found == switches.end() ? std::nullopt : std::optional(found->second);
        });
        for (char c : line) {
            if (c == 'n' || c == 'N')
                switches[u"System.Xml.XmlResolver.IsNetworkingEnabledByDefault"] = c == 'N';
            else if (c == 'd' || c == 'D')
                switches[u"TestSwitch.LocalAppContext.DisableCaching"] = c == 'D';
            else if (c == 'g')
                std::cout << (policy.networking_enabled_by_default() ? 1 : 0) << ","
                          << policy.cache_snapshot() << ";";
        }
        std::cout << '\n';
    }
}

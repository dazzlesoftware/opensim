// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Reuse controlled data/account providers from the operation gate.
#define PROFILES_PROVIDER_ONLY
#include "profiles_service_probe.cpp"
#undef PROFILES_PROVIDER_ONLY
#include "opensim/services/profiles_configuration.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
std::string U(std::string_view s) {
    if (s == "-" || s == ".")
        return {};
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
std::string text(const UserProfilesService::Text& value) {
    return value ? H(utf8(*value)) : "N";
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        char op;
        int mode, flags;
        std::string rawname, raw;
        in >> op >> mode >> flags >> rawname >> raw;
        auto name = rawname == "-" ? UserProfilesService::Text{}
                                   : UserProfilesService::Text(utf16(U(rawname == "." ? "" : rawname)));
        std::string trace, log, status, state = "-";
        auto store = std::make_shared<Store>(0);
        auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
        auto config = arena->create_ini_source(nini_string_reader(utf16(U(raw))));
        auto dispatcher = std::make_shared<server::JsonRpcDispatcher>();
        if (flags & 4)
            dispatcher->add(u"classified_update", [](const auto&, auto&) { return true; });
        try {
            if (op == 'S') {
                ProfilesPluginFactories factory;
                factory.storage = [&](auto module, auto connection) -> std::shared_ptr<ProfilesData> {
                    trace += "D:" + text(module) + ":" + text(connection) + ";";
                    if (mode == 2)
                        throw std::runtime_error("storage factory failure");
                    return mode == 1 ? nullptr : store;
                };
                factory.accounts = [&](auto module, auto) -> std::shared_ptr<ProfileAccounts> {
                    trace += "A:" + text(module) + ";";
                    if (mode == 4)
                        throw std::runtime_error("account factory failure");
                    return {};
                };
                auto service = configured_profiles_service(
                    flags & 1 ? nullptr : config, name, factory, [&](auto level, auto message) {
                        log += (level == ProfilesDiagnosticLevel::Warn ? "W:" : "E:") + utf8(message) + "\n";
                    });
                state = service->data ? "D" : "N";
            } else {
                ProfileServiceConnector connector(
                    flags & 1 ? nullptr : config, flags & 2 ? nullptr : dispatcher, name,
                    [&](auto module, auto, auto section) -> std::shared_ptr<UserProfilesService> {
                        trace += "S:" + text(module) + ":" + text(section) + ";";
                        if (mode == 6)
                            throw std::runtime_error("service factory failure");
                        return mode == 5 ? nullptr : std::make_shared<ProfilesService>(store);
                    });
                state = (connector.enabled ? "1" : "0") + std::string(connector.server ? "1" : "0") +
                        (connector.service_module ? "1" : "0") + ":" + text(connector.config_name);
            }
            status = "OK";
        } catch (const std::exception& e) {
            status = "EX:" + H(e.what());
        }
        std::string keys;
        for (auto key : dispatcher->keys())
            keys += utf8(key) + ";";
        std::cout << status << "|" << state << "|" << H(trace) << "|" << H(log) << "|" << H(keys) << "\n";
    }
}

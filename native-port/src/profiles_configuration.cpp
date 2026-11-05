// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/profiles_configuration.hpp"
namespace opensim::services {
namespace {
using namespace framework;
using Text = UserProfilesService::Text;
template <class T> T& required(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *value;
}
bool empty(const Text& value) {
    return !value || value->empty();
}
Text get(const std::shared_ptr<NiniConfig>& section, const char16_t* key, const Text& fallback) {
    auto value =
        required(section).get_string(nini_writer_text(std::u16string(key)), nini_writer_text(fallback));
    return value ? Text(*value) : Text{};
}
std::shared_ptr<NiniConfig> section(const std::shared_ptr<NiniConfigSourceBase>& config, const Text& name) {
    return required(config).configs()->at(nini_writer_text(name));
}
}
std::shared_ptr<ProfilesService> configured_profiles_service(std::shared_ptr<NiniConfigSourceBase> config,
                                                             Text name,
                                                             const ProfilesPluginFactories& factories,
                                                             ProfilesDiagnostic log) {
    std::shared_ptr<ProfilesData> data;
    if (empty(name)) {
        if (log)
            log(ProfilesDiagnosticLevel::Warn, u"[PROFILES SERVICE]: Configuration section not given!");
    } else {
        Text module = u"", connection, realm = u"";
        if (auto database = section(config, u"DatabaseService")) {
            module = get(database, u"StorageProvider", u"");
            connection = get(database, u"ConnectionString", u"");
        }
        if (auto profiles = section(config, name)) {
            module = get(profiles, u"StorageProvider", module);
            connection = get(profiles, u"ConnectionString", connection);
            realm = get(profiles, u"Realm", realm);
        }
        (void)realm; // Read by source, but never passed to its provider constructor.
        if (factories.storage)
            data = factories.storage(module, connection);
        if (!data)
            throw std::runtime_error("Could not find a storage interface in the given module");
    }
    std::shared_ptr<ProfileAccounts> accounts;
    if (auto profiles = section(config, name)) {
        auto module = get(profiles, u"UserAccountService", u"");
        if (module != Text(u"") && factories.accounts)
            accounts = factories.accounts(module, config);
    } else if (log)
        log(ProfilesDiagnosticLevel::Warn, u"[PROFILES SERVICE]: No configuration found!");
    return std::make_shared<ProfilesService>(std::move(data), std::move(accounts), [log](auto text) {
        if (log)
            log(ProfilesDiagnosticLevel::Error, text);
    });
}
ProfileServiceConnector::ProfileServiceConnector(std::shared_ptr<NiniConfigSourceBase> config,
                                                 std::shared_ptr<server::JsonRpcDispatcher> dispatcher,
                                                 Text name, Factory factory,
                                                 server::JsonRpcProfileHandlers::Logger log) {
    config_name = empty(name) ? Text(u"UserProfilesService") : name;
    auto profiles = section(config, config_name);
    if (!profiles)
        throw std::runtime_error("No section " + framework::utf8(*config_name) + " in config file");
    if (!profiles->get_boolean(nini_writer_text(u"Enabled"), false))
        return;
    enabled = true;
    server = std::move(dispatcher);
    auto module = get(profiles, u"LocalServiceModule", u"");
    if (factory)
        service_module = factory(module, config, config_name);
    server::JsonRpcProfileHandlers handlers(service_module, std::move(log));
    handlers.register_handlers(required(server));
}
}

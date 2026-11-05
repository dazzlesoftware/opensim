// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/local_profiles_connector.hpp"
namespace opensim::services {
namespace {
template <class T> T& required(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *value;
}
}
LocalProfilesConnector::LocalProfilesConnector(ServerGetter server, Factory factory, Logger log,
                                               server::JsonRpcProfileHandlers::Logger handlers)
    : server_(std::move(server)), factory_(std::move(factory)), log_(std::move(log)),
      handler_log_(std::move(handlers)) {
}
LocalProfilesConnector::LocalProfilesConnector(std::shared_ptr<framework::NiniConfigSourceBase> source,
                                               ServerGetter server, Factory factory, Logger log,
                                               server::JsonRpcProfileHandlers::Logger handlers)
    : LocalProfilesConnector(std::move(server), std::move(factory), std::move(log), std::move(handlers)) {
    initialise_service(std::move(source));
}
void LocalProfilesConnector::initialise_service(std::shared_ptr<framework::NiniConfigSourceBase> source) {
    config_name = u"UserProfilesService";
    auto server = server_ ? server_()
                          : nullptr; // MainServer.Instance is read before configuration, even when disabled.
    auto config = required(source).configs()->at(framework::nini_writer_text(config_name));
    if (!config)
        return;
    if (!config->get_boolean(framework::nini_writer_text(u"Enabled"), false)) {
        enabled = false;
        return;
    }
    enabled = true;
    auto module = config->get_string(framework::nini_writer_text(u"LocalServiceModule"),
                                     framework::nini_writer_text(u""));
    if (!module)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    if (module->empty()) {
        if (log_)
            log_(
                LocalProfileLogLevel::Error,
                u"[LOCAL USERPROFILES SERVICE CONNECTOR]: No LocalServiceModule named in section UserProfilesService");
        return;
    }
    service_module = factory_ ? factory_(*module, source, config_name) : nullptr;
    if (!service_module) {
        if (log_)
            log_(LocalProfileLogLevel::Error,
                 u"[LOCAL USERPROFILES SERVICE CONNECTOR]: Can't load user profiles service");
        return;
    }
    enabled = true;
    server::JsonRpcProfileHandlers handlers(service_module, handler_log_);
    handlers.register_handlers(required(server));
}
void LocalProfilesConnector::initialise(std::shared_ptr<framework::NiniConfigSourceBase> source) {
    auto config = required(source).configs()->at(framework::nini_writer_text(u"Modules"));
    if (config) {
        auto selected = config->get_string(framework::nini_writer_text(u"UserProfilesServices"),
                                           framework::nini_writer_text(u""));
        if (selected && *selected == name) {
            initialise_service(source);
            if (log_)
                log_(LocalProfileLogLevel::Info,
                     u"[LOCAL USERPROFILES SERVICE CONNECTOR]: Local user profiles connector enabled");
        }
    }
}
void LocalProfilesConnector::add_region(std::shared_ptr<LocalProfileScene> scene) {
    if (!enabled)
        return;
    std::lock_guard lock(mutex_);
    if (regions_.contains(required(scene).region_id())) {
        if (log_)
            log_(
                LocalProfileLogLevel::Error,
                u"[LOCAL USERPROFILES SERVICE CONNECTOR]: simulator seems to have more than one region with the same UUID. Please correct this!");
    } else {
        auto key = required(scene).region_id();
        if (regions_.contains(key))
            throw std::invalid_argument("An item with the same key has already been added. Key: " +
                                        key.str());
        regions_[key] = std::move(scene);
    }
}
void LocalProfilesConnector::remove_region(std::shared_ptr<LocalProfileScene> scene) {
    if (!enabled)
        return;
    std::lock_guard lock(mutex_);
    if (regions_.contains(required(scene).region_id()))
        regions_.erase(required(scene).region_id());
}
}

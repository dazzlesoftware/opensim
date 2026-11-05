// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/profiles_configuration.hpp"
namespace opensim::services {
class LocalProfileScene {
public:
    virtual ~LocalProfileScene() = default;
    virtual framework::Uuid region_id() const = 0;
};
enum class LocalProfileLogLevel { Info, Error };
struct LocalProfileConnectorProbe;
class LocalProfilesConnector {
public:
    using Text = UserProfilesService::Text;
    using ServerGetter = std::function<std::shared_ptr<server::JsonRpcDispatcher>()>;
    using Factory = ProfileServiceConnector::Factory;
    using Logger = std::function<void(LocalProfileLogLevel, std::u16string_view)>;
    LocalProfilesConnector(ServerGetter, Factory, Logger = {}, server::JsonRpcProfileHandlers::Logger = {});
    LocalProfilesConnector(std::shared_ptr<framework::NiniConfigSourceBase>, ServerGetter, Factory,
                           Logger = {}, server::JsonRpcProfileHandlers::Logger = {});
    static constexpr std::u16string_view name = u"LocalUserProfilesServicesConnector";
    Text config_name;
    bool enabled = false;
    std::shared_ptr<UserProfilesService> service_module;
    void initialise_service(std::shared_ptr<framework::NiniConfigSourceBase>);
    void initialise(std::shared_ptr<framework::NiniConfigSourceBase>);
    void post_initialise() const {}
    void close() const {}
    void add_region(std::shared_ptr<LocalProfileScene>);
    void remove_region(std::shared_ptr<LocalProfileScene>);
    void region_loaded(std::shared_ptr<LocalProfileScene>) const {}

private:
    friend struct LocalProfileConnectorProbe;
    ServerGetter server_;
    Factory factory_;
    Logger log_;
    server::JsonRpcProfileHandlers::Logger handler_log_;
    mutable std::recursive_mutex mutex_;
    framework::OrderedDictionary<framework::Uuid, std::shared_ptr<LocalProfileScene>, framework::UuidHash>
        regions_;
};
}

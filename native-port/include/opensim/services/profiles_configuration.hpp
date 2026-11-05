// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/profiles_service.hpp"
#include "opensim/framework/nini_config.hpp"
#include "opensim/server/profile_handlers.hpp"
namespace opensim::services {
enum class ProfilesDiagnosticLevel {Warn,Error};
using ProfilesDiagnostic=std::function<void(ProfilesDiagnosticLevel,std::u16string_view)>;
struct ProfilesPluginFactories {
 using Text=UserProfilesService::Text;
 std::function<std::shared_ptr<ProfilesData>(Text module,Text connection)> storage;
 std::function<std::shared_ptr<ProfileAccounts>(Text module,std::shared_ptr<framework::NiniConfigSourceBase>)> accounts;
};
// Models both original service constructors. Plugin resolution stays explicit so
// the executable can select registered native providers without a CLR runtime.
std::shared_ptr<ProfilesService> configured_profiles_service(
 std::shared_ptr<framework::NiniConfigSourceBase>,UserProfilesService::Text section,
 const ProfilesPluginFactories&,ProfilesDiagnostic={});
class ProfileServiceConnector {
public:
 using Text=UserProfilesService::Text;
 using Factory=std::function<std::shared_ptr<UserProfilesService>(Text module,std::shared_ptr<framework::NiniConfigSourceBase>,Text section)>;
 ProfileServiceConnector(std::shared_ptr<framework::NiniConfigSourceBase>,std::shared_ptr<server::JsonRpcDispatcher>,Text section,Factory,server::JsonRpcProfileHandlers::Logger={});
 Text config_name;
 bool enabled=false;
 std::shared_ptr<server::JsonRpcDispatcher> server;
 std::shared_ptr<UserProfilesService> service_module;
};
}

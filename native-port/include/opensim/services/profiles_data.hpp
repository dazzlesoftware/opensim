// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/user_profile_models.hpp"
namespace opensim::services {
class ProfilesData {
public:
 using Text=std::optional<std::u16string>;
 virtual ~ProfilesData()=default;
 virtual std::shared_ptr<network::Value> GetClassifiedRecords(framework::Uuid id)=0;
 virtual bool UpdateClassifiedRecord(std::shared_ptr<framework::UserClassifiedAdd> value,Text& result)=0;
 virtual bool GetClassifiedInfo(std::shared_ptr<framework::UserClassifiedAdd>& value,Text& result)=0;
 virtual bool DeleteClassifiedRecord(framework::Uuid id)=0;
 virtual std::shared_ptr<network::Value> GetAvatarPicks(framework::Uuid id)=0;
 virtual std::shared_ptr<framework::UserProfilePick> GetPickInfo(framework::Uuid avatar,framework::Uuid pick)=0;
 virtual bool UpdatePicksRecord(std::shared_ptr<framework::UserProfilePick> value)=0;
 virtual bool DeletePicksRecord(framework::Uuid id)=0;
 virtual bool GetAvatarNotes(std::shared_ptr<framework::UserProfileNotes>& value)=0;
 virtual bool UpdateAvatarNotes(std::shared_ptr<framework::UserProfileNotes>& value,Text& result)=0;
 virtual bool GetAvatarProperties(std::shared_ptr<framework::UserProfileProperties>& value,Text& result)=0;
 virtual bool UpdateAvatarProperties(std::shared_ptr<framework::UserProfileProperties>& value,Text& result)=0;
 virtual bool UpdateAvatarInterests(std::shared_ptr<framework::UserProfileProperties> value,Text& result)=0;
 virtual bool UpdateUserPreferences(std::shared_ptr<framework::UserPreferences>& value,Text& result)=0;
 virtual bool GetUserPreferences(std::shared_ptr<framework::UserPreferences>& value,Text& result)=0;
 virtual std::shared_ptr<network::Value> GetUserImageAssets(framework::Uuid id)=0;
 virtual bool GetUserAppData(std::shared_ptr<framework::UserAppData>& value,Text& result)=0;
 virtual bool SetUserAppData(std::shared_ptr<framework::UserAppData> value,Text& result)=0;
};
}

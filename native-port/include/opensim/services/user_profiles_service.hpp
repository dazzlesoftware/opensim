// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/user_profile_models.hpp"
namespace opensim::services {
// A reference to shared_ptr preserves C# ref-object replacement, including null.
class UserProfilesService {
  public:
    using Text = std::optional<std::u16string>;
    virtual ~UserProfilesService() = default;
    virtual std::shared_ptr<network::Value> AvatarClassifiedsRequest(framework::Uuid) = 0;
    virtual bool ClassifiedUpdate(std::shared_ptr<framework::UserClassifiedAdd>, Text&) = 0;
    virtual bool ClassifiedInfoRequest(std::shared_ptr<framework::UserClassifiedAdd>&, Text&) = 0;
    virtual bool ClassifiedDelete(framework::Uuid) = 0;
    virtual std::shared_ptr<network::Value> AvatarPicksRequest(framework::Uuid) = 0;
    virtual bool PickInfoRequest(std::shared_ptr<framework::UserProfilePick>&, Text&) = 0;
    virtual bool PicksUpdate(std::shared_ptr<framework::UserProfilePick>&, Text&) = 0;
    virtual bool PicksDelete(framework::Uuid) = 0;
    virtual bool AvatarNotesRequest(std::shared_ptr<framework::UserProfileNotes>&) = 0;
    virtual bool NotesUpdate(std::shared_ptr<framework::UserProfileNotes>&, Text&) = 0;
    virtual bool AvatarPropertiesRequest(std::shared_ptr<framework::UserProfileProperties>&, Text&) = 0;
    virtual bool AvatarPropertiesUpdate(std::shared_ptr<framework::UserProfileProperties>&, Text&) = 0;
    virtual bool UserPreferencesRequest(std::shared_ptr<framework::UserPreferences>&, Text&) = 0;
    virtual bool UserPreferencesUpdate(std::shared_ptr<framework::UserPreferences>&, Text&) = 0;
    virtual bool AvatarInterestsUpdate(std::shared_ptr<framework::UserProfileProperties>, Text&) = 0;
    virtual std::shared_ptr<network::Value> AvatarImageAssetsRequest(framework::Uuid) = 0;
    virtual bool RequestUserAppData(std::shared_ptr<framework::UserAppData>&, Text&) = 0;
    virtual bool SetUserAppData(std::shared_ptr<framework::UserAppData>, Text&) = 0;
};
}

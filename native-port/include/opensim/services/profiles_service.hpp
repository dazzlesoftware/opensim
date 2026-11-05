// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/user_profiles_service.hpp"
#include "opensim/services/profiles_data.hpp"
namespace opensim::services {
struct ProfileAccount {
    std::optional<std::u16string> email;
};
class ProfileAccounts {
  public:
    virtual ~ProfileAccounts() = default;
    virtual std::shared_ptr<ProfileAccount> get(framework::Uuid scope, framework::Uuid user) = 0;
};
// The source admits only the local concrete UserAccountService (and subclasses).
// Remote/account connector implementations remain outside this marker hierarchy.
class LocalProfileAccounts : public ProfileAccounts {};
class ProfilesService : public UserProfilesService {
  public:
    using Logger = std::function<void(std::u16string_view)>;
    ProfilesService(std::shared_ptr<ProfilesData> data, std::shared_ptr<ProfileAccounts> accounts = {},
                    Logger logger = {})
        : data(std::move(data)), accounts_(std::move(accounts)), logger_(std::move(logger)) {}
    std::shared_ptr<ProfilesData> data;
    std::shared_ptr<network::Value> AvatarClassifiedsRequest(framework::Uuid id) override;
    bool ClassifiedUpdate(std::shared_ptr<framework::UserClassifiedAdd> value, Text& result) override;
    bool ClassifiedInfoRequest(std::shared_ptr<framework::UserClassifiedAdd>& value, Text& result) override;
    bool ClassifiedDelete(framework::Uuid id) override;
    std::shared_ptr<network::Value> AvatarPicksRequest(framework::Uuid id) override;
    bool PickInfoRequest(std::shared_ptr<framework::UserProfilePick>& value, Text& result) override;
    bool PicksUpdate(std::shared_ptr<framework::UserProfilePick>& value, Text& result) override;
    bool PicksDelete(framework::Uuid id) override;
    bool AvatarNotesRequest(std::shared_ptr<framework::UserProfileNotes>& value) override;
    bool NotesUpdate(std::shared_ptr<framework::UserProfileNotes>& value, Text& result) override;
    bool AvatarPropertiesRequest(std::shared_ptr<framework::UserProfileProperties>& value,
                                 Text& result) override;
    bool AvatarPropertiesUpdate(std::shared_ptr<framework::UserProfileProperties>& value,
                                Text& result) override;
    bool AvatarInterestsUpdate(std::shared_ptr<framework::UserProfileProperties> value,
                               Text& result) override;
    bool UserPreferencesUpdate(std::shared_ptr<framework::UserPreferences>& value, Text& result) override;
    bool UserPreferencesRequest(std::shared_ptr<framework::UserPreferences>& value, Text& result) override;
    std::shared_ptr<network::Value> AvatarImageAssetsRequest(framework::Uuid id) override;
    bool RequestUserAppData(std::shared_ptr<framework::UserAppData>& value, Text& result) override;
    bool SetUserAppData(std::shared_ptr<framework::UserAppData> value, Text& result) override;

  private:
    std::shared_ptr<ProfileAccounts> accounts_;
    Logger logger_;
    bool account_error(Text&, bool exception);
};
}

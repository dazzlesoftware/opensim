// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/profiles_service.hpp"
namespace opensim::services {
using namespace framework;
using network::Value;
namespace {
template <class T> T& required(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *value;
}
bool empty(const std::optional<std::u16string>& value) {
    return !value || value->empty();
}
}
std::shared_ptr<network::Value> ProfilesService::AvatarClassifiedsRequest(framework::Uuid id) {
    return required(data).GetClassifiedRecords(id);
}
bool ProfilesService::ClassifiedUpdate(std::shared_ptr<framework::UserClassifiedAdd> value, Text& result) {
    if (!required(data).UpdateClassifiedRecord(value, result))
        return false;
    result = u"success";
    return true;
}
bool ProfilesService::ClassifiedInfoRequest(std::shared_ptr<framework::UserClassifiedAdd>& value,
                                            Text& result) {
    return required(data).GetClassifiedInfo(value, result);
}
bool ProfilesService::ClassifiedDelete(framework::Uuid id) {
    return required(data).DeleteClassifiedRecord(id);
}
std::shared_ptr<network::Value> ProfilesService::AvatarPicksRequest(framework::Uuid id) {
    return required(data).GetAvatarPicks(id);
}
bool ProfilesService::PickInfoRequest(std::shared_ptr<framework::UserProfilePick>& value, Text& result) {
    auto creator = required(value).CreatorId;
    auto pick = required(value).PickId;
    value = required(data).GetPickInfo(creator, pick);
    result = u"OK";
    return true;
}
bool ProfilesService::PicksUpdate(std::shared_ptr<framework::UserProfilePick>& value, Text& result) {
    (void)result;
    return required(data).UpdatePicksRecord(value);
}
bool ProfilesService::PicksDelete(framework::Uuid id) {
    return required(data).DeletePicksRecord(id);
}
bool ProfilesService::AvatarNotesRequest(std::shared_ptr<framework::UserProfileNotes>& value) {
    return required(data).GetAvatarNotes(value);
}
bool ProfilesService::NotesUpdate(std::shared_ptr<framework::UserProfileNotes>& value, Text& result) {
    return required(data).UpdateAvatarNotes(value, result);
}
bool ProfilesService::AvatarPropertiesRequest(std::shared_ptr<framework::UserProfileProperties>& value,
                                              Text& result) {
    return required(data).GetAvatarProperties(value, result);
}
bool ProfilesService::AvatarPropertiesUpdate(std::shared_ptr<framework::UserProfileProperties>& value,
                                             Text& result) {
    return required(data).UpdateAvatarProperties(value, result);
}
bool ProfilesService::AvatarInterestsUpdate(std::shared_ptr<framework::UserProfileProperties> value,
                                            Text& result) {
    return required(data).UpdateAvatarInterests(value, result);
}
std::shared_ptr<network::Value> ProfilesService::AvatarImageAssetsRequest(framework::Uuid id) {
    return required(data).GetUserImageAssets(id);
}
bool ProfilesService::RequestUserAppData(std::shared_ptr<framework::UserAppData>& value, Text& result) {
    return required(data).GetUserAppData(value, result);
}
bool ProfilesService::SetUserAppData(std::shared_ptr<framework::UserAppData> value, Text& result) {
    (void)value;
    (void)result;
    return true;
}
bool ProfilesService::account_error(Text& result, bool exception) {
    if (logger_)
        logger_(exception ? u"[PROFILES SERVICE]: UserAccountService Exception: Could not get user account"
                          : u"[PROFILES SERVICE]: UserAccountService: Could not get user account");
    result = u"UserAccountService settings error in UserProfileService!";
    return false;
}
bool ProfilesService::UserPreferencesUpdate(std::shared_ptr<UserPreferences>& value, Text& result) {
    if (empty(required(value).EMail)) {
        if (dynamic_cast<LocalProfileAccounts*>(accounts_.get())) {
            try {
                auto account = accounts_->get({}, required(value).UserId);
                required(value).EMail = empty(required(account).email) ? Text(u"") : required(account).email;
            } catch (...) {
                return account_error(result, true);
            }
        } else
            return account_error(result, false);
    }
    return required(data).UpdateUserPreferences(value, result);
}
bool ProfilesService::UserPreferencesRequest(std::shared_ptr<UserPreferences>& value, Text& result) {
    if (!required(data).GetUserPreferences(value, result))
        return false;
    if (empty(required(value).EMail)) {
        if (dynamic_cast<LocalProfileAccounts*>(accounts_.get())) {
            try {
                auto account = accounts_->get({}, required(value).UserId);
                if (empty(required(account).email))
                    required(value).EMail = u"";
                else {
                    required(value).EMail = required(account).email;
                    UserPreferencesUpdate(value, result);
                }
            } catch (...) {
                return account_error(result, true);
            }
        } else
            return account_error(result, false);
    }
    if (empty(required(value).EMail))
        value->EMail = u"No Email Address On Record";
    return true;
}
}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/profiles_data.hpp"
#include "opensim/services/sqlite_generic_connection.hpp"
#include "opensim/framework/asset_base.hpp"
namespace opensim::services {
class SqliteProfilesData : public ProfilesData {
  public:
    using Logger = std::function<void(std::u16string_view)>;
    using Clock = std::function<framework::AssetDateTime()>;
    explicit SqliteProfilesData(Logger logger = {}, Clock clock = {})
        : logger_(std::move(logger)), clock_(std::move(clock)) {}
    SqliteProfilesData(const std::string& connection, Logger logger = {}, Clock clock = {});
    void initialise(const std::string& connection);
    std::shared_ptr<SqliteDatabase> database() const {
        return database_;
    }
    std::shared_ptr<network::Value> GetClassifiedRecords(framework::Uuid id) override;
    bool UpdateClassifiedRecord(std::shared_ptr<framework::UserClassifiedAdd> value, Text& result) override;
    bool GetClassifiedInfo(std::shared_ptr<framework::UserClassifiedAdd>& value, Text& result) override;
    bool DeleteClassifiedRecord(framework::Uuid id) override;
    std::shared_ptr<network::Value> GetAvatarPicks(framework::Uuid id) override;
    std::shared_ptr<framework::UserProfilePick> GetPickInfo(framework::Uuid avatar,
                                                            framework::Uuid pick) override;
    bool UpdatePicksRecord(std::shared_ptr<framework::UserProfilePick> value) override;
    bool DeletePicksRecord(framework::Uuid id) override;
    bool GetAvatarNotes(std::shared_ptr<framework::UserProfileNotes>& value) override;
    bool UpdateAvatarNotes(std::shared_ptr<framework::UserProfileNotes>& value, Text& result) override;
    bool GetAvatarProperties(std::shared_ptr<framework::UserProfileProperties>& value, Text& result) override;
    bool UpdateAvatarProperties(std::shared_ptr<framework::UserProfileProperties>& value,
                                Text& result) override;
    bool UpdateAvatarInterests(std::shared_ptr<framework::UserProfileProperties> value,
                               Text& result) override;
    bool UpdateUserPreferences(std::shared_ptr<framework::UserPreferences>& value, Text& result) override;
    bool GetUserPreferences(std::shared_ptr<framework::UserPreferences>& value, Text& result) override;
    std::shared_ptr<network::Value> GetUserImageAssets(framework::Uuid id) override;
    bool GetUserAppData(std::shared_ptr<framework::UserAppData>& value, Text& result) override;
    bool SetUserAppData(std::shared_ptr<framework::UserAppData> value, Text& result) override;

  private:
    std::shared_ptr<SqliteDatabase> database_;
    Logger logger_;
    Clock clock_;
    void error(std::string_view operation, const std::exception&) const;
};
}

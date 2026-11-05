// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/user_account.hpp"
#include "opensim/services/sqlite_generic_table.hpp"
namespace opensim::services {
class SqliteDatabase;
using SqliteFriendsQueryState = SqliteQueryState;
struct FriendInfo {
    framework::Uuid principal;
    std::optional<std::string> friend_id;
    std::int32_t my_flags = 0, their_flags = 0;
    FriendInfo() = default;
    explicit FriendInfo(const AccountValues& values);
    AccountValues to_key_value_pairs() const;
};
struct FriendsData {
    using Fields = framework::OrderedDictionary<std::string, std::optional<std::string>>;
    std::optional<std::string> principal, friend_id;
    std::shared_ptr<Fields> data;
};
class FriendsStore {
  public:
    virtual ~FriendsStore() = default;
    virtual std::vector<std::shared_ptr<FriendsData>> get(const std::optional<std::string>& principal) = 0;
    virtual std::vector<std::shared_ptr<FriendsData>> get(const framework::Uuid& principal) {
        return get(std::optional<std::string>(principal.str()));
    }
    virtual bool store(const std::shared_ptr<FriendsData>& row) = 0;
    virtual bool erase(const std::optional<std::string>& principal,
                       const std::optional<std::string>& friend_id) = 0;
    virtual bool erase(const framework::Uuid& principal, const std::optional<std::string>& friend_id) {
        return erase(std::optional<std::string>(principal.str()), friend_id);
    }
};
class SqliteFriendsStore final : public FriendsStore {
  public:
    explicit SqliteFriendsStore(const std::string& path, bool initialize = false,
                                const std::string& realm = "Friends");
    SqliteFriendsStore(std::shared_ptr<SqliteDatabase> connection, const std::string& realm,
                       std::shared_ptr<SqliteFriendsQueryState> query_state = {});
    ~SqliteFriendsStore();
    using FriendsStore::get;
    using FriendsStore::erase;
    std::vector<std::shared_ptr<FriendsData>> get(const std::optional<std::string>& principal) override;
    std::vector<std::shared_ptr<FriendsData>> get_field(std::optional<std::string> field,
                                                        std::optional<std::string> key);
    std::vector<std::shared_ptr<FriendsData>> get_fields(const SqliteTableStrings* fields,
                                                         const SqliteTableStrings* keys);
    std::vector<std::shared_ptr<FriendsData>> get_where(std::optional<std::string> where);
    bool delete_fields(const SqliteTableStrings* fields, const SqliteTableStrings* keys);
    bool store(const std::shared_ptr<FriendsData>& row) override;
    bool erase(const std::optional<std::string>& principal,
               const std::optional<std::string>& friend_id) override;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
class IFriendsService {
  public:
    virtual ~IFriendsService() = default;
    virtual std::vector<FriendInfo> get(const framework::Uuid& principal) = 0;
    virtual std::vector<FriendInfo> get(const std::optional<std::string>& principal) = 0;
    virtual bool store(const std::optional<std::string>& principal,
                       const std::optional<std::string>& friend_id, std::int32_t flags) = 0;
    virtual bool erase(const framework::Uuid& principal, const std::optional<std::string>& friend_id) = 0;
    virtual bool erase(const std::optional<std::string>& principal,
                       const std::optional<std::string>& friend_id) = 0;
};
bool friend_can_modify_objects(IFriendsService& service, framework::Uuid actor, framework::Uuid owner,
                               bool hypergrid);
class FriendsService : public IFriendsService {
  public:
    explicit FriendsService(FriendsStore& store) : store_(store) {}
    std::vector<FriendInfo> get(const framework::Uuid& principal);
    std::vector<FriendInfo> get(const std::optional<std::string>& principal);
    bool store(const std::optional<std::string>& principal, const std::optional<std::string>& friend_id,
               std::int32_t flags);
    bool erase(const framework::Uuid& principal, const std::optional<std::string>& friend_id) {
        return store_.erase(principal, friend_id);
    }
    bool erase(const std::optional<std::string>& principal, const std::optional<std::string>& friend_id) {
        return store_.erase(principal, friend_id);
    }

  private:
    FriendsStore& store_;
};
}

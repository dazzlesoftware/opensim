// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/avatar_data.hpp"
#include <functional>

namespace opensim::services {
struct AvatarBaseData {
    framework::Uuid principal;
    std::shared_ptr<AvatarData::Fields> data;
};

class AvatarStore {
  public:
    virtual ~AvatarStore() = default;
    virtual std::vector<std::shared_ptr<AvatarBaseData>> get(framework::Uuid principal) = 0;
    virtual bool store(std::shared_ptr<AvatarBaseData> row) = 0;
    virtual bool erase_all(framework::Uuid principal) = 0;
    virtual bool erase(framework::Uuid principal, const std::optional<std::string> &name) = 0;

    virtual bool write_appearance(const std::function<bool()> &write) {
        return write();
    }
};

class SqliteAvatarStore final : public AvatarStore {
  public:
    explicit SqliteAvatarStore(const std::string &path, bool initialize = false, const std::string &realm = "Avatars");
    ~SqliteAvatarStore();
    std::vector<std::shared_ptr<AvatarBaseData>> get(framework::Uuid principal) override;
    bool store(std::shared_ptr<AvatarBaseData> row) override;
    bool erase_all(framework::Uuid principal) override;
    bool erase(framework::Uuid principal, const std::optional<std::string> &name) override;
    bool write_appearance(const std::function<bool()> &write) override;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

class AvatarService {
  public:
    using Strings = std::vector<std::optional<std::string>>;

    explicit AvatarService(AvatarStore &store) : store_(store) {}

    virtual ~AvatarService() = default;

    virtual AvatarData get_avatar(framework::Uuid principal);
    virtual bool set_avatar(framework::Uuid principal, AvatarData &avatar);
    virtual std::shared_ptr<framework::AvatarAppearance> get_appearance(framework::Uuid principal);
    virtual bool set_appearance(framework::Uuid principal, const framework::AvatarAppearance *appearance);

    virtual bool reset(framework::Uuid principal) {
        return store_.erase_all(principal);
    }

    virtual bool set_items(framework::Uuid principal, const Strings *names, const Strings *values);
    virtual bool remove_items(framework::Uuid principal, const Strings *names);

  private:
    AvatarStore &store_;
};
} // namespace opensim::services

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/avatar_data.hpp"
namespace opensim::services {
struct AvatarBaseData {
    framework::Uuid principal;
    std::shared_ptr<AvatarData::Fields> data;
};
class AvatarStore {
public:
    virtual ~AvatarStore()=default;
    virtual std::vector<std::shared_ptr<AvatarBaseData>> get(framework::Uuid principal)=0;
    virtual bool store(std::shared_ptr<AvatarBaseData> row)=0;
    virtual bool erase_all(framework::Uuid principal)=0;
    virtual bool erase(framework::Uuid principal,const std::optional<std::string>& name)=0;
};
class SqliteAvatarStore final:public AvatarStore {
public:
    explicit SqliteAvatarStore(const std::string& path,bool initialize=false,const std::string& realm="Avatars");
    ~SqliteAvatarStore();
    std::vector<std::shared_ptr<AvatarBaseData>> get(framework::Uuid principal)override;
    bool store(std::shared_ptr<AvatarBaseData> row)override;
    bool erase_all(framework::Uuid principal)override;
    bool erase(framework::Uuid principal,const std::optional<std::string>& name)override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
class AvatarService {
public:
    using Strings=std::vector<std::optional<std::string>>;
    explicit AvatarService(AvatarStore& store):store_(store){}
    AvatarData get_avatar(framework::Uuid principal);
    bool set_avatar(framework::Uuid principal,AvatarData& avatar);
    std::shared_ptr<framework::AvatarAppearance> get_appearance(framework::Uuid principal);
    bool set_appearance(framework::Uuid principal,const framework::AvatarAppearance* appearance);
    bool reset(framework::Uuid principal){return store_.erase_all(principal);}
    bool set_items(framework::Uuid principal,const Strings* names,const Strings* values);
    bool remove_items(framework::Uuid principal,const Strings* names);
private:
    AvatarStore& store_;
};
}

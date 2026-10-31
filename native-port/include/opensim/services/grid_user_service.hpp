/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>
namespace opensim::services {
struct GridUserData {
    using Fields=framework::OrderedDictionary<std::string,std::optional<std::string>>;
    std::optional<std::string> user_id;
    std::shared_ptr<Fields> data=std::make_shared<Fields>();
};
struct GridUserInfo {
    std::string user_id;
    framework::Uuid home_region,last_region;
    framework::Vector3 home_position,home_look_at,last_position,last_look_at;
    bool online=false;
    std::int32_t login=0,logout=0;
};
class GridUserStore {
public:
    virtual ~GridUserStore()=default;
    virtual std::shared_ptr<GridUserData> get(const std::string& user)=0;
    virtual std::vector<std::shared_ptr<GridUserData>> get_all(const std::string& prefix)=0;
    virtual bool store(const std::shared_ptr<GridUserData>& data)=0;
};
class SqliteGridUserStore final:public GridUserStore {
public:
    explicit SqliteGridUserStore(const std::string& path,bool initialize=false,const std::string& realm="GridUser");
    ~SqliteGridUserStore();
    std::shared_ptr<GridUserData> get(const std::string& user)override;
    std::vector<std::shared_ptr<GridUserData>> get_all(const std::string& prefix)override;
    bool store(const std::shared_ptr<GridUserData>& data)override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
class GridUserCache {
public:
    using Clock=std::function<double()>;
    explicit GridUserCache(Clock clock={});
    ~GridUserCache();
    void purge();
private:
    struct State;
    std::unique_ptr<State> state_;
    friend class GridUserService;
};
class GridUserService {
public:
    using UnixClock=std::function<std::int32_t()>;
    explicit GridUserService(GridUserStore& store,UnixClock clock={},std::shared_ptr<GridUserCache> cache={});
    std::optional<GridUserInfo> info(const std::string& user);
    std::vector<std::optional<GridUserInfo>> info(const std::vector<std::string>& users);
    GridUserInfo logged_in(const std::string& user);
    bool logged_out(const std::string& user,const framework::Uuid& session,const framework::Uuid& region,const framework::Vector3& position,const framework::Vector3& look_at);
    bool set_home(const std::string& user,const framework::Uuid& region,const framework::Vector3& position,const framework::Vector3& look_at);
    bool set_last_position(const std::string& user,const framework::Uuid& session,const framework::Uuid& region,const framework::Vector3& position,const framework::Vector3& look_at);
    int recently_online();
private:
    std::shared_ptr<GridUserData> data(const std::string& user);
    GridUserInfo to_info(const GridUserData& data);
    void update_cache(const std::string& user,const std::shared_ptr<GridUserData>& data);
    GridUserStore& store_;
    UnixClock clock_;
    std::shared_ptr<GridUserCache> cache_;
};
}

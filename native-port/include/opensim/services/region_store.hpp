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
#include "opensim/services/grid_region.hpp"
#include <memory>
namespace opensim::services {
using RegionRows=std::vector<std::shared_ptr<RegionData>>;
class RegionStore {
public:
    virtual ~RegionStore()=default;
    virtual std::shared_ptr<RegionData> get(const framework::Uuid& id,const framework::Uuid& scope)=0;
    virtual std::shared_ptr<RegionData> at(std::int32_t x,std::int32_t y,const framework::Uuid& scope)=0;
    virtual std::shared_ptr<RegionData> specific(const std::string& name,const framework::Uuid& scope)=0;
    virtual std::optional<RegionRows> find(const std::string& pattern,const framework::Uuid& scope)=0;
    virtual RegionRows range(std::int32_t x1,std::int32_t y1,std::int32_t x2,std::int32_t y2,const framework::Uuid& scope)=0;
    virtual RegionRows with_flags(std::int32_t flags,const framework::Uuid& scope)=0;
    virtual bool store(const std::shared_ptr<RegionData>& row)=0;
    virtual bool set(const framework::Uuid& id,const std::string& item,const std::string& value)=0;
    virtual bool erase(const framework::Uuid& id)=0;
};
class NullRegionStore final:public RegionStore {
public:
    explicit NullRegionStore(const std::string& connection="");
    std::shared_ptr<RegionData> get(const framework::Uuid& id,const framework::Uuid& scope)override;
    std::shared_ptr<RegionData> at(std::int32_t x,std::int32_t y,const framework::Uuid& scope)override;
    std::shared_ptr<RegionData> specific(const std::string& name,const framework::Uuid& scope)override;
    std::optional<RegionRows> find(const std::string& pattern,const framework::Uuid& scope)override;
    RegionRows range(std::int32_t x1,std::int32_t y1,std::int32_t x2,std::int32_t y2,const framework::Uuid& scope)override;
    RegionRows with_flags(std::int32_t flags,const framework::Uuid& scope)override;
    bool store(const std::shared_ptr<RegionData>& row)override;
    bool set(const framework::Uuid& id,const std::string& item,const std::string& value)override;
    bool erase(const framework::Uuid& id)override;
private:
    struct State;std::shared_ptr<State> state_;
    static std::shared_ptr<State> singleton(bool create=false);
};
class SqliteRegionStore final:public RegionStore {
public:
    explicit SqliteRegionStore(const std::string& path,bool initialize=false,const std::string& realm="regions");
    ~SqliteRegionStore();
    std::shared_ptr<RegionData> get(const framework::Uuid& id,const framework::Uuid& scope)override;
    std::shared_ptr<RegionData> at(std::int32_t x,std::int32_t y,const framework::Uuid& scope)override;
    std::shared_ptr<RegionData> specific(const std::string& name,const framework::Uuid& scope)override;
    std::optional<RegionRows> find(const std::string& pattern,const framework::Uuid& scope)override;
    RegionRows range(std::int32_t x1,std::int32_t y1,std::int32_t x2,std::int32_t y2,const framework::Uuid& scope)override;
    RegionRows with_flags(std::int32_t flags,const framework::Uuid& scope)override;
    bool store(const std::shared_ptr<RegionData>& row)override;
    bool set(const framework::Uuid& id,const std::string& item,const std::string& value)override;
    bool erase(const framework::Uuid& id)override;
private:
    struct State;std::unique_ptr<State> state_;
};

}

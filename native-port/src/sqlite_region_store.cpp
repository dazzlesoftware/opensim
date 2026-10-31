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
#include "opensim/services/region_store.hpp"
#include "opensim/framework/filesystem.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <bit>
#include <filesystem>
#include <mutex>
#include <set>
#include <stdexcept>
namespace opensim::services {
namespace {
std::string quoted(const std::string& name){std::string value="\"";for(auto c:name){if(c=='\0')throw std::invalid_argument("Invalid SQLite identifier");if(c=='\"')value+='\"';value+=c;}return value+'\"';}
std::int32_t add(std::int32_t a,std::int32_t b){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));}
struct Statement {
 sqlite3* db;sqlite3_stmt* value=nullptr;
 Statement(sqlite3* database,const std::string& sql):db(database){if(sqlite3_prepare_v2(db,sql.c_str(),-1,&value,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 ~Statement(){sqlite3_finalize(value);}
 void bind(int index,const std::optional<std::string>& text){const auto result=text?sqlite3_bind_text(value,index,text->data(),static_cast<int>(text->size()),SQLITE_TRANSIENT):sqlite3_bind_null(value,index);if(result!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 bool row(){const auto result=sqlite3_step(value);if(result==SQLITE_ROW)return true;if(result==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(db));}
 std::string text(int index){const auto* bytes=sqlite3_column_text(value,index);return bytes?std::string(reinterpret_cast<const char*>(bytes),sqlite3_column_bytes(value,index)):std::string{};}
};
framework::Uuid uuid(const std::string& value){try{return framework::Uuid(value);}catch(...){return {};}}
}
struct SqliteRegionStore::State {
 sqlite3* db=nullptr;std::recursive_mutex mutex;std::string table;std::set<std::string> columns;
 ~State(){if(db)sqlite3_close(db);}
 RegionRows query(std::string condition,std::vector<std::optional<std::string>> arguments,const framework::Uuid& scope,const std::string& order=""){
  if(!scope.is_zero()){condition+=" AND ScopeID=?";arguments.push_back(scope.str());}Statement statement(db,"SELECT * FROM "+table+" WHERE "+condition+order);int index=1;for(const auto& argument:arguments)statement.bind(index++,argument);RegionRows result;
  while(statement.row()){auto row=std::make_shared<RegionData>();for(int i=0;i<sqlite3_column_count(statement.value);++i){const std::string name=sqlite3_column_name(statement.value,i);const auto text=statement.text(i);if(name=="uuid")row->id=uuid(text);else if(name=="ScopeID")row->scope=uuid(text);else if(name=="regionName")row->name=text;else if(name=="locX")row->x=parse_region_integer(text);else if(name=="locY")row->y=parse_region_integer(text);else row->data[name]=text;}row->size_x=parse_region_integer(row->data.at("sizeX"));row->size_y=parse_region_integer(row->data.at("sizeY"));result.push_back(row);}return result;
 }
};
SqliteRegionStore::SqliteRegionStore(const std::string& path,bool initialize,const std::string& realm):state_(std::make_unique<State>()){
 if(path.empty()||realm.empty())throw std::invalid_argument("Region database and realm must not be empty");state_->table=quoted(realm);
 if(initialize&&path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Region initialization requires an empty destination");
 const auto flags=SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX|(initialize?SQLITE_OPEN_CREATE:0);if(sqlite3_open_v2(path.c_str(),&state_->db,flags,nullptr)!=SQLITE_OK)throw std::runtime_error(state_->db?sqlite3_errmsg(state_->db):"SQLite allocation failed");sqlite3_busy_timeout(state_->db,5000);
 if(initialize){Statement create(state_->db,"CREATE TABLE "+state_->table+"(uuid TEXT NOT NULL PRIMARY KEY,regionHandle TEXT,regionName VARCHAR(128) NOT NULL,regionRecvKey VARCHAR(128),regionSendKey VARCHAR(128),regionSecret VARCHAR(128),regionDataURI VARCHAR(128),serverIP VARCHAR(64),serverPort INTEGER,serverURI VARCHAR(255),locX INTEGER,locY INTEGER,locZ INTEGER,eastOverrideHandle TEXT,westOverrideHandle TEXT,southOverrideHandle TEXT,northOverrideHandle TEXT,regionAssetURI VARCHAR(255),regionAssetRecvKey VARCHAR(128),regionAssetSendKey VARCHAR(128),regionUserURI VARCHAR(255),regionUserRecvKey VARCHAR(128),regionUserSendKey VARCHAR(128),regionMapTexture TEXT,serverHttpPort INTEGER,serverRemotingPort INTEGER,owner_uuid TEXT NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',originUUID TEXT NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',access INTEGER DEFAULT 0,ScopeID TEXT DEFAULT '00000000-0000-0000-0000-000000000000',sizeX INTEGER NOT NULL DEFAULT 0,sizeY INTEGER NOT NULL DEFAULT 0,flags INTEGER NOT NULL DEFAULT 0,last_seen INTEGER NOT NULL DEFAULT 0,PrincipalID TEXT NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',Token VARCHAR(255) NOT NULL DEFAULT '0',parcelMapTexture TEXT)");create.row();}
 Statement columns(state_->db,"PRAGMA table_info("+state_->table+")");while(columns.row())state_->columns.insert(columns.text(1));for(const auto* name:{"uuid","ScopeID","regionName","locX","locY","sizeX","sizeY","flags"})if(!state_->columns.contains(name))throw std::runtime_error("Region table is missing required columns");
}
SqliteRegionStore::~SqliteRegionStore()=default;
std::shared_ptr<RegionData> SqliteRegionStore::get(const framework::Uuid& id,const framework::Uuid& scope){std::lock_guard lock(state_->mutex);auto rows=state_->query("uuid=?",{id.str()},scope);return rows.empty()?nullptr:rows.front();}
std::shared_ptr<RegionData> SqliteRegionStore::specific(const std::string& name,const framework::Uuid& scope){std::lock_guard lock(state_->mutex);auto rows=state_->query("lower(regionName)=lower(?)",{name},scope);return rows.empty()?nullptr:rows.front();}
std::optional<RegionRows> SqliteRegionStore::find(const std::string& pattern,const framework::Uuid& scope){std::lock_guard lock(state_->mutex);return state_->query("lower(regionName) LIKE lower(?) ESCAPE '\\'",{pattern},scope," ORDER BY lower(regionName)");}
std::shared_ptr<RegionData> SqliteRegionStore::at(std::int32_t x,std::int32_t y,const framework::Uuid& scope){std::lock_guard lock(state_->mutex);auto rows=state_->query("locX BETWEEN ? AND ? AND locY BETWEEN ? AND ?",{std::to_string(add(x,-4096)),std::to_string(x),std::to_string(add(y,-4096)),std::to_string(y)},scope);for(const auto& row:rows)if(x>=row->x&&x<add(row->x,row->size_x)&&y>=row->y&&y<add(row->y,row->size_y))return row;return nullptr;}
RegionRows SqliteRegionStore::range(std::int32_t x1,std::int32_t y1,std::int32_t x2,std::int32_t y2,const framework::Uuid& scope){std::lock_guard lock(state_->mutex);auto rows=state_->query("locX BETWEEN ? AND ? AND locY BETWEEN ? AND ?",{std::to_string(add(x1,-4096)),std::to_string(x2),std::to_string(add(y1,-4096)),std::to_string(y2)},scope);std::erase_if(rows,[&](const auto& row){return !(add(row->x,row->size_x)>x1&&row->x<=x2&&add(row->y,row->size_y)>y1&&row->y<=y2);});return rows;}
RegionRows SqliteRegionStore::with_flags(std::int32_t flags,const framework::Uuid& scope){std::lock_guard lock(state_->mutex);return state_->query("(flags & ?)<>0",{std::to_string(flags)},scope);}
bool SqliteRegionStore::store(const std::shared_ptr<RegionData>& row){
 if(!row)throw std::invalid_argument("Missing region data");std::lock_guard lock(state_->mutex);RegionFields data=row->data;for(const auto* key:{"uuid","ScopeID","regionName","posX","posY","sizeX","sizeY","locX","locY"})data.erase(key);
 std::string names="uuid,ScopeID,regionName,locX,locY,sizeX,sizeY",values="?,?,?,?,?,?,?",updates="locX=excluded.locX,locY=excluded.locY,sizeX=excluded.sizeX,sizeY=excluded.sizeY";
 for(const auto& [key,value]:data){if(!state_->columns.contains(key))throw std::invalid_argument("Unknown region data column");names+=','+quoted(key);values+=",?";updates+=','+quoted(key)+"=excluded."+quoted(key);}
 Statement statement(state_->db,"INSERT INTO "+state_->table+"("+names+") VALUES("+values+") ON CONFLICT(uuid) DO UPDATE SET "+updates+(row->scope.is_zero()?"":" WHERE ScopeID=excluded.ScopeID"));int index=1;for(const auto& value:std::vector<std::optional<std::string>>{row->id.str(),row->scope.str(),row->name,std::to_string(row->x),std::to_string(row->y),std::to_string(row->size_x),std::to_string(row->size_y)})statement.bind(index++,value);for(const auto& [key,value]:data)statement.bind(index++,value);statement.row();return sqlite3_changes(state_->db)>0;
}
bool SqliteRegionStore::set(const framework::Uuid& id,const std::string& key,const std::string& value){std::lock_guard lock(state_->mutex);if(!state_->columns.contains(key)||key=="uuid")throw std::invalid_argument("Unknown or immutable region data column");Statement statement(state_->db,"UPDATE "+state_->table+" SET "+quoted(key)+"=? WHERE uuid=?");statement.bind(1,value);statement.bind(2,id.str());statement.row();return sqlite3_changes(state_->db)>0;}
bool SqliteRegionStore::erase(const framework::Uuid& id){std::lock_guard lock(state_->mutex);Statement statement(state_->db,"DELETE FROM "+state_->table+" WHERE uuid=?");statement.bind(1,id.str());statement.row();return sqlite3_changes(state_->db)>0;}
}

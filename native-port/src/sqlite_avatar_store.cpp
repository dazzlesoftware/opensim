// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/avatar_service.hpp"
#include "opensim/framework/filesystem.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <mutex>
#include <algorithm>
namespace opensim::services {
namespace {
class Statement {
    sqlite3* db_;sqlite3_stmt* value_=nullptr;
public:
    Statement(sqlite3* db,const std::string& sql):db_(db){if(sqlite3_prepare_v2(db,sql.c_str(),-1,&value_,nullptr)!=SQLITE_OK){auto error=std::string(sqlite3_errmsg(db));if(value_)sqlite3_finalize(value_);throw std::runtime_error(error);}}
    ~Statement(){sqlite3_finalize(value_);}
    void bind(int index,const std::optional<std::string>& text){auto result=text?sqlite3_bind_text(value_,index,text->data(),static_cast<int>(text->size()),SQLITE_TRANSIENT):sqlite3_bind_null(value_,index);if(result!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db_));}
    bool row(){auto result=sqlite3_step(value_);if(result==SQLITE_ROW)return true;if(result==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(db_));}
    std::string text(int col)const{auto value=sqlite3_column_text(value_,col);return value?std::string(reinterpret_cast<const char*>(value),sqlite3_column_bytes(value_,col)):std::string{};}
    int columns()const{return sqlite3_column_count(value_);}std::string name(int col)const{return sqlite3_column_name(value_,col);}
};
}
struct SqliteAvatarStore::State{
    sqlite3* db=nullptr;std::string realm;std::recursive_mutex mutex;
    ~State(){if(db)sqlite3_close(db);}
};
SqliteAvatarStore::SqliteAvatarStore(const std::string& path,bool initialize,const std::string& realm):state_(std::make_unique<State>()){
    if(path.empty()||realm.empty())throw std::invalid_argument("Empty avatar database path or realm");state_->realm=realm;
    if(initialize&&path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Avatar initialization requires an empty destination");
    if(sqlite3_open_v2(path.c_str(),&state_->db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX|(initialize?SQLITE_OPEN_CREATE:0),nullptr)!=SQLITE_OK)throw std::runtime_error(state_->db?sqlite3_errmsg(state_->db):"SQLite allocation failed");sqlite3_busy_timeout(state_->db,5000);
    if(initialize){Statement create(state_->db,"CREATE TABLE "+realm+" (PrincipalID CHAR(36) NOT NULL, Name VARCHAR(32) NOT NULL, Value VARCHAR(255) NOT NULL DEFAULT '', PRIMARY KEY(PrincipalID,Name))");create.row();}
    Statement check(state_->db,"SELECT PrincipalID,Name,Value FROM "+realm+" LIMIT 0");check.row();
}
SqliteAvatarStore::~SqliteAvatarStore()=default;
std::vector<std::shared_ptr<AvatarBaseData>> SqliteAvatarStore::get(framework::Uuid principal){
    std::lock_guard lock(state_->mutex);Statement query(state_->db,"SELECT * FROM "+state_->realm+" WHERE PrincipalID=?");query.bind(1,principal.str());std::vector<std::shared_ptr<AvatarBaseData>> result;
    while(query.row()){auto row=std::make_shared<AvatarBaseData>();row->data=std::make_shared<AvatarData::Fields>();for(int i=0;i<query.columns();++i){auto name=query.name(i);if(name=="PrincipalID")framework::try_parse_sdk_uuid(query.text(i),row->principal);else (*row->data)[name]=query.text(i);}result.push_back(row);}return result;
}
bool SqliteAvatarStore::store(std::shared_ptr<AvatarBaseData> row){
    if(!row||!row->data)throw std::invalid_argument("Null avatar row");std::string sql="REPLACE INTO "+state_->realm+" (PrincipalID";auto fields=row->data;for(auto [key,value]:*fields)sql+=",`"+key+"`";sql+=") VALUES (?";for(std::size_t i=0;i<fields->size();++i)sql+=",?";sql+=")";
    std::lock_guard lock(state_->mutex);Statement command(state_->db,sql);command.bind(1,row->principal.str());int index=2;for(auto [key,value]:*fields)command.bind(index++,value);command.row();return sqlite3_changes(state_->db)>0;
}
bool SqliteAvatarStore::erase_all(framework::Uuid principal){std::lock_guard lock(state_->mutex);Statement command(state_->db,"DELETE FROM "+state_->realm+" WHERE PrincipalID=?");command.bind(1,principal.str());command.row();return sqlite3_changes(state_->db)>0;}
bool SqliteAvatarStore::erase(framework::Uuid principal,const std::optional<std::string>& name){std::lock_guard lock(state_->mutex);Statement command(state_->db,"DELETE FROM "+state_->realm+" WHERE PrincipalID=? AND Name=?");command.bind(1,principal.str());command.bind(2,name);command.row();return sqlite3_changes(state_->db)>0;}
}

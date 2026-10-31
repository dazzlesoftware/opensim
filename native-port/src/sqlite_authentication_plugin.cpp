// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_authentication_plugin.hpp"
#include <sqlite3.h>
#include <bit>
#include <chrono>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace opensim::services {
namespace {
using Parameters=std::vector<std::pair<std::string,std::any>>;
std::int32_t tick(){
#ifdef _WIN32
 return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(GetTickCount()));
#else
 auto count=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(count));
#endif
}
std::string object_text(const std::any& value){if(!value.has_value()||value.type()==typeid(std::nullptr_t))throw std::invalid_argument("Null authentication value");if(value.type()==typeid(std::vector<std::uint8_t>))return "System.Byte[]";if(auto p=std::any_cast<const char*>(&value);p&&!*p)throw std::invalid_argument("Null authentication string");return framework::service_url_object_text(value);}
struct Statement {sqlite3_stmt* value=nullptr;~Statement(){if(value)sqlite3_finalize(value);}};
void bind(sqlite3_stmt* s,const Parameters& params){std::map<std::string,std::any> values;for(auto& [key,value]:params)values[key]=value;for(int i=1;i<=sqlite3_bind_parameter_count(s);++i){auto name=sqlite3_bind_parameter_name(s,i);if(!name||!values.contains(name))throw std::invalid_argument("Insufficient authentication parameters");auto& value=values.at(name);int rc=SQLITE_MISUSE;
 if(!value.has_value()||value.type()==typeid(std::nullptr_t))rc=sqlite3_bind_null(s,i);
 else if(auto v=std::any_cast<bool>(&value))rc=sqlite3_bind_int64(s,i,*v?1:0);
 else if(auto v=std::any_cast<std::int32_t>(&value))rc=sqlite3_bind_int64(s,i,*v);
 else if(auto v=std::any_cast<std::uint32_t>(&value))rc=sqlite3_bind_int64(s,i,*v);
 else if(auto v=std::any_cast<std::int64_t>(&value))rc=sqlite3_bind_int64(s,i,*v);
 else if(auto v=std::any_cast<std::uint64_t>(&value))rc=sqlite3_bind_int64(s,i,std::bit_cast<std::int64_t>(*v));
 else if(auto v=std::any_cast<float>(&value))rc=sqlite3_bind_double(s,i,*v);
 else if(auto v=std::any_cast<double>(&value))rc=sqlite3_bind_double(s,i,*v);
 else if(auto v=std::any_cast<std::vector<std::uint8_t>>(&value))rc=v->empty()?sqlite3_bind_zeroblob(s,i,0):sqlite3_bind_blob(s,i,v->data(),static_cast<int>(v->size()),SQLITE_TRANSIENT);
 else {auto text=object_text(value);rc=sqlite3_bind_text(s,i,text.data(),static_cast<int>(text.size()),SQLITE_TRANSIENT);}
 if(rc!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s)));}}
std::shared_ptr<SqliteDatabase> required(const SqliteTableOperations::Connection& callback){auto db=callback?callback():nullptr;if(!db||!db->handle())throw std::runtime_error("Authentication database is not open");return db;}
}
SqliteAuthenticationTable::SqliteAuthenticationTable(SqliteTableOperations::Connection connection,std::string realm,Clock clock,Diagnostic diagnostic,std::string schema):connection_(std::move(connection)),realm_(std::move(realm)),schema_(std::move(schema)),clock_(clock?std::move(clock):Clock{tick}),diagnostic_(std::move(diagnostic)){}
std::string SqliteAuthenticationTable::table_name(const std::string& name)const{return schema_.empty()?"`"+name+"`":"`"+schema_+"`.`"+name+"`";}
int SqliteAuthenticationTable::execute(const std::string& sql,const Parameters& parameters){auto db=required(connection_);std::lock_guard lock(db->mutex());const char* at=sql.c_str();int changed=0;while(*at){Statement s;const char* tail=nullptr;if(sqlite3_prepare_v2(db->handle(),at,-1,&s.value,&tail)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db->handle()));if(tail==at)break;at=tail;if(!s.value)continue;bind(s.value,parameters);for(;;){int rc=sqlite3_step(s.value);if(rc==SQLITE_DONE)break;if(rc!=SQLITE_ROW)throw std::runtime_error(sqlite3_errmsg(db->handle()));}if(!sqlite3_stmt_readonly(s.value))changed+=sqlite3_changes(db->handle());}return changed;}
std::shared_ptr<AuthenticationRecord> SqliteAuthenticationTable::get(framework::Uuid principal){auto result=std::make_shared<AuthenticationRecord>();result->data=std::make_shared<framework::OrderedDictionary<std::string,std::any>>();auto db=required(connection_);std::lock_guard lock(db->mutex());Statement s;auto sql="select * from "+table_name(realm_)+" where UUID = :PrincipalID";if(sqlite3_prepare_v2(db->handle(),sql.c_str(),-1,&s.value,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db->handle()));bind(s.value,{{":PrincipalID",principal.str()}});int rc=sqlite3_step(s.value);if(rc==SQLITE_DONE)return {};if(rc!=SQLITE_ROW)throw std::runtime_error(sqlite3_errmsg(db->handle()));auto* reader=s.value;auto names=db->reader_columns(sql);if(!names){names.emplace();for(int i=0;i<sqlite3_column_count(reader);++i)names->push_back(sqlite3_column_name(reader,i));}db->retain_reader(reader,sql,*names);s.value=nullptr;try{result->principal=principal;std::lock_guard columns_lock(columns_mutex_);if(!columns_)columns_=*names;SqliteRow row(reader);for(auto& name:*columns_)if(name!="UUID")(*result->data)[name]=row.value_text(row.column(name));return result;}catch(...){return {};}}
bool SqliteAuthenticationTable::store(const std::shared_ptr<AuthenticationRecord>& record){if(!record||!record->data)throw std::invalid_argument("Null authentication record");record->data->erase("UUID");std::vector<std::string> fields;for(auto& [name,value]:*record->data)fields.push_back(name);auto values=record->data;std::vector<std::string> unused(values->size());size_t i=0;for(auto& [name,value]:*values)unused.at(i++)=object_text(value);
 bool update=static_cast<bool>(get(record->principal));Parameters parameters;std::string sql;if(update){sql="update "+table_name(realm_)+" set ";bool first=true;for(auto& field:fields){if(!first)sql+=", ";sql+="`"+field+"` = :"+field;parameters.emplace_back(":"+field,record->data->at(field));first=false;}sql+=" where UUID = :UUID";parameters.emplace_back(":UUID",record->principal.str());}
 else {std::string names,slots;for(size_t n=0;n<fields.size();++n){if(n){names+="`, `";slots+=", :";}names+=fields[n];slots+=fields[n];}sql="insert into "+table_name(realm_)+" (`UUID`, `"+names+"`) values (:UUID, :"+slots+")";parameters.emplace_back(":UUID",record->principal.str());for(auto& field:fields)parameters.emplace_back(":"+field,record->data->at(field));}
 try{return execute(sql,parameters)>=1;}catch(const std::exception& e){if(diagnostic_)diagnostic_(update?"error":"console",update?"[SQLITE]: Exception storing authentication data":"",e);else if(update)std::cerr<<"[SQLITE]: Exception storing authentication data: "<<e.what()<<'\n';else std::cout<<e.what()<<'\n';return false;}}
bool SqliteAuthenticationTable::set_data_item(framework::Uuid principal,const std::optional<std::string>& item,const std::optional<std::string>& value){return execute("update "+table_name(realm_)+" set `"+item.value_or("")+"` = "+value.value_or("")+" where UUID = '"+principal.str()+"'")>0;}
void SqliteAuthenticationTable::expire_if_needed(){auto now=clock_();auto elapsed=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now)-static_cast<std::uint32_t>(last_expire_.load()));if(elapsed>30000){execute("delete from "+table_name("tokens")+" where validity < datetime('now', 'localtime')");last_expire_=clock_();}}
bool SqliteAuthenticationTable::set_token(framework::Uuid principal,const std::optional<std::string>& token,std::int32_t lifetime){expire_if_needed();return execute("insert into "+table_name("tokens")+" (UUID, token, validity) values ('"+principal.str()+"', '"+token.value_or("")+"', datetime('now', 'localtime', '+"+std::to_string(lifetime)+" minutes'))")>0;}
bool SqliteAuthenticationTable::check_token(framework::Uuid principal,const std::optional<std::string>& token,std::int32_t lifetime){expire_if_needed();return execute("update "+table_name("tokens")+" set validity = datetime('now', 'localtime', '+"+std::to_string(lifetime)+" minutes') where UUID = '"+principal.str()+"' and token = '"+token.value_or("")+"' and validity > datetime('now', 'localtime')")>0;}
std::shared_ptr<SqliteGenericState> SqliteAuthenticationPlugin::shared_state(){static auto state=std::make_shared<SqliteGenericState>();return state;}
SqliteAuthenticationPlugin::SqliteAuthenticationPlugin(const std::string& connection,std::string realm,MigrationLog log,Clock clock,Diagnostic diagnostic,std::shared_ptr<SqliteGenericState> state,int):SqliteAuthenticationTable([state]{return state->connection.load();},std::move(realm),std::move(clock),std::move(diagnostic)){initialize_generic_sqlite(state,connection,"AuthStore",auth_store_migrations(),std::move(log));}
}

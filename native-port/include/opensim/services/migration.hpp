// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/unicode_regex.hpp"
#include <functional>
#include <map>
namespace opensim::services {
class MigrationReader {
public:virtual ~MigrationReader()=default;virtual bool read()=0;virtual int32_t version_value()=0;virtual void close()=0;virtual void dispose()=0;
};
class MigrationCommand {
public:virtual ~MigrationCommand()=default;virtual void timeout(int32_t seconds)=0;virtual void text(const std::string& sql)=0;virtual void execute()=0;virtual std::shared_ptr<MigrationReader> reader()=0;virtual void dispose()=0;
};
class MigrationDatabase {
public:virtual ~MigrationDatabase()=default;virtual std::shared_ptr<MigrationCommand> command()=0;
};
class MigrationResources {
public:virtual ~MigrationResources()=default;virtual std::vector<std::string> names()=0;virtual std::optional<std::string> bytes(const std::string& name)=0;
};
struct MigrationLog {std::function<void(const std::string&)> info,debug,error;};
class Migration {
public:
 using Scripts=std::map<int32_t,std::vector<std::string>>;
 Migration()=default;
 Migration(std::shared_ptr<MigrationDatabase> database,std::shared_ptr<MigrationResources> resources,std::optional<std::string> type,MigrationLog log={});
 Migration(std::shared_ptr<MigrationDatabase> database,std::shared_ptr<MigrationResources> resources,std::optional<std::string> subtype,std::optional<std::string> type,MigrationLog log={});
 virtual ~Migration()=default;
 void initialize(std::shared_ptr<MigrationDatabase> database,std::shared_ptr<MigrationResources> resources,std::optional<std::string> type,std::optional<std::string> subtype);
 void init_migrations_table();
 void update();
 int32_t version();void version(int32_t value);
 Scripts migrations_after(int32_t after);
 virtual int32_t find_version(std::shared_ptr<MigrationDatabase> database,const std::optional<std::string>& type);
 virtual void execute_script(std::shared_ptr<MigrationDatabase> database,const std::vector<std::string>& sql);
protected:std::optional<std::string> type_;std::shared_ptr<MigrationDatabase> database_;std::shared_ptr<MigrationResources> resources_;
private:
 void execute(const std::string& sql);void execute(const std::vector<std::string>& sql);void insert_version(const std::optional<std::string>& type,int32_t version);void update_version(const std::optional<std::string>& type,int32_t version);
 std::shared_ptr<framework::UnicodeRegex> old_,merged_;MigrationLog log_;
};
}

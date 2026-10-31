// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/migration.hpp"
struct sqlite3;
namespace opensim::services {
// The connection owner must outlive the migration and its commands/readers.
std::shared_ptr<MigrationDatabase> sqlite_migration_database(sqlite3* connection);
std::shared_ptr<MigrationResources> asset_store_migrations();
std::shared_ptr<MigrationResources> friends_store_migrations();
std::shared_ptr<MigrationResources> avatar_store_migrations();
std::shared_ptr<MigrationResources> grid_user_store_migrations();
std::shared_ptr<MigrationResources> user_account_store_migrations();
std::shared_ptr<MigrationResources> auth_store_migrations();
}

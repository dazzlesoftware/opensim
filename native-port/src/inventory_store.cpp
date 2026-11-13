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
#include "opensim/services/inventory_store.hpp"
#include "opensim/framework/filesystem.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <bit>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
namespace opensim::services {
using framework::Uuid;
namespace {
struct Statement {
    sqlite3 *db;
    sqlite3_stmt *value = nullptr;
    Statement(sqlite3 *database, const std::string &sql) : db(database) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &value, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() {
        sqlite3_finalize(value);
    }
    void bind(int index, const std::string &text) {
        if (sqlite3_bind_text(value, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT) !=
            SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    void bind(int index, int number) {
        if (sqlite3_bind_int(value, index, number) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    bool row() {
        const auto result = sqlite3_step(value);
        if (result == SQLITE_ROW)
            return true;
        if (result == SQLITE_DONE)
            return false;
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    std::string text(int index) {
        const auto *bytes = sqlite3_column_text(value, index);
        return bytes ? std::string(reinterpret_cast<const char *>(bytes), sqlite3_column_bytes(value, index))
                     : std::string{};
    }
    int integer(int index) {
        return sqlite3_column_int(value, index);
    }
};
std::string truncate_name(const std::string &name, std::size_t limit = 64) {
    std::size_t pos = 0, units = 0;
    while (pos < name.size() && units < limit) {
        const auto first = static_cast<unsigned char>(name[pos]);
        const std::size_t length = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        if (pos + length > name.size())
            throw std::invalid_argument("Truncated UTF-8 inventory name");
        const auto needed = length == 4 ? 2u : 1u;
        if (units + needed > limit)
            return name.substr(0, pos) + "\xef\xbf\xbd";
        units += needed;
        pos += length;
    }
    return name.substr(0, pos);
}
const std::vector<std::string> item_columns = {"assetID",
                                               "assetType",
                                               "inventoryName",
                                               "inventoryDescription",
                                               "inventoryNextPermissions",
                                               "inventoryCurrentPermissions",
                                               "invType",
                                               "creatorID",
                                               "inventoryBasePermissions",
                                               "inventoryEveryOnePermissions",
                                               "salePrice",
                                               "saleType",
                                               "creationDate",
                                               "groupID",
                                               "groupOwned",
                                               "flags",
                                               "inventoryID",
                                               "avatarID",
                                               "parentFolderID",
                                               "inventoryGroupPermissions"};
std::string item_column_list() {
    std::string value;
    for (const auto &name : item_columns) {
        if (!value.empty())
            value += ',';
        value += '`' + name + '`';
    }
    return value;
}
std::string where(const InventoryFolderStore::Filter &filter, bool item = false) {
    if (filter.empty())
        throw std::invalid_argument("Inventory folder queries require a filter");
    std::string sql;
    for (const auto &[key, value] : filter) {
        const bool valid =
            item ? std::find(item_columns.begin(), item_columns.end(), key) != item_columns.end()
                 : (key == "folderName" || key == "type" || key == "version" || key == "folderID" ||
                    key == "agentID" || key == "parentFolderID");
        if (!valid)
            throw std::invalid_argument("Unknown inventory column");
        sql += sql.empty() ? " WHERE " : " AND ";
        sql += '`' + key + "`=?";
    }
    return sql;
}
void bind_filter(Statement &statement, const InventoryFolderStore::Filter &filter) {
    int i = 1;
    for (const auto &pair : filter)
        statement.bind(i++, pair.second);
}
InventoryFolder framework_folder(InventoryFolder folder) {
    folder.type = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(folder.type));
    folder.version = static_cast<std::uint16_t>(folder.version);
    return folder;
}
}
struct InventoryFolderStore::Impl {
    sqlite3 *db = nullptr;
    std::recursive_mutex mutex;
    ~Impl() {
        if (db)
            sqlite3_close(db);
    }
    void execute(const std::string &sql) {
        Statement statement(db, sql);
        statement.row();
    }
    void increment(const Uuid &parent) {
        Statement statement(db, "UPDATE inventoryfolders SET version=version+1 WHERE folderID=?");
        statement.bind(1, parent.str());
        statement.row();
    }
};
InventoryFolderStore::InventoryFolderStore(const std::string &path, bool initialize)
    : impl_(std::make_unique<Impl>()) {
    if (path.empty())
        throw std::invalid_argument("Inventory database path is empty");
    if (initialize && path != ":memory:" && std::filesystem::exists(framework::utf8_path(path)) &&
        std::filesystem::file_size(framework::utf8_path(path)) > 0)
        throw std::runtime_error("Inventory initialization requires an empty destination");
    const auto flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | (initialize ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(path.c_str(), &impl_->db, flags, nullptr) != SQLITE_OK)
        throw std::runtime_error(impl_->db ? sqlite3_errmsg(impl_->db) : "Cannot allocate SQLite connection");
    sqlite3_busy_timeout(impl_->db, 5000);
    if (initialize) {
        impl_->execute("BEGIN TRANSACTION");
        impl_->execute(
            "CREATE TABLE inventoryfolders(folderName varchar(64),type integer,version integer,folderID "
            "varchar(36) primary key,agentID varchar(36) not null default "
            "'00000000-0000-0000-0000-000000000000',parentFolderID varchar(36) not null default "
            "'00000000-0000-0000-0000-000000000000')");
        impl_->execute("CREATE INDEX inventoryfolders_agentid ON inventoryfolders(agentID)");
        impl_->execute("CREATE INDEX inventoryfolders_parentid ON inventoryfolders(parentFolderID)");
        impl_->execute(
            "CREATE TABLE inventoryitems(assetID varchar(36),assetType integer,inventoryName varchar(64),"
            "inventoryDescription varchar(128),inventoryNextPermissions integer,inventoryCurrentPermissions "
            "integer,invType integer,creatorID varchar(128),inventoryBasePermissions integer,"
            "inventoryEveryOnePermissions integer,salePrice integer default 99,saleType integer default 0,"
            "creationDate integer default 2000,groupID varchar(36) default "
            "'00000000-0000-0000-0000-000000000000',groupOwned integer default 0,flags integer default 0,"
            "inventoryID varchar(36) primary key,parentFolderID varchar(36) not null default "
            "'00000000-0000-0000-0000-000000000000',avatarID varchar(36) not null default "
            "'00000000-0000-0000-0000-000000000000',inventoryGroupPermissions integer not null default 0)");
        impl_->execute("CREATE INDEX inventoryitems_parentfolderid ON inventoryitems(parentFolderID)");
        impl_->execute("CREATE INDEX inventoryitems_avatarid ON inventoryitems(avatarID)");
        impl_->execute("COMMIT");
    }
    Statement check(
        impl_->db,
        "SELECT folderName,type,version,folderID,agentID,parentFolderID FROM inventoryfolders LIMIT 0");
    check.row();
}
InventoryFolderStore::~InventoryFolderStore() = default;
std::vector<InventoryFolder> InventoryFolderStore::folders(const Filter &filter) {
    std::lock_guard lock(impl_->mutex);
    Statement statement(
        impl_->db, "SELECT folderName,type,version,folderID,agentID,parentFolderID FROM inventoryfolders" +
                       where(filter));
    bind_filter(statement, filter);
    std::vector<InventoryFolder> result;
    while (statement.row())
        result.push_back({statement.text(0), statement.integer(1), statement.integer(2),
                          Uuid(statement.text(3)), Uuid(statement.text(4)), Uuid(statement.text(5))});
    return result;
}
bool InventoryFolderStore::store(InventoryFolder &folder, bool replace) {
    std::lock_guard lock(impl_->mutex);
    folder.name = truncate_name(folder.name);
    Statement statement(
        impl_->db,
        replace
            ? "REPLACE INTO inventoryfolders(folderName,type,version,folderID,agentID,parentFolderID) VALUES(?,?,?,?,?,?)"
            : "INSERT OR IGNORE INTO inventoryfolders(folderName,type,version,folderID,agentID,parentFolderID) VALUES(?,?,?,?,?,?)");
    statement.bind(1, folder.name);
    statement.bind(2, folder.type);
    statement.bind(3, folder.version);
    statement.bind(4, folder.id.str());
    statement.bind(5, folder.owner.str());
    statement.bind(6, folder.parent.str());
    statement.row();
    if (sqlite3_changes(impl_->db) == 0)
        return false;
    impl_->increment(folder.parent);
    return true;
}
bool InventoryFolderStore::move(const Uuid &id, const Uuid &parent) {
    std::lock_guard lock(impl_->mutex);
    auto old = folders({{"folderID", id.str()}});
    if (old.empty())
        return false;
    Statement statement(impl_->db, "UPDATE inventoryfolders SET parentFolderID=? WHERE folderID=?");
    statement.bind(1, parent.str());
    statement.bind(2, id.str());
    statement.row();
    if (sqlite3_changes(impl_->db) == 0)
        return false;
    impl_->increment(old.front().parent);
    impl_->increment(parent);
    return true;
}
bool InventoryFolderStore::erase(const Filter &filter) {
    std::lock_guard lock(impl_->mutex);
    Statement statement(impl_->db, "DELETE FROM inventoryfolders" + where(filter));
    bind_filter(statement, filter);
    statement.row();
    return sqlite3_changes(impl_->db) > 0;
}
std::vector<InventoryItem> InventoryFolderStore::items(const Filter &filter) {
    std::lock_guard lock(impl_->mutex);
    Statement statement(impl_->db,
                        "SELECT " + item_column_list() + " FROM inventoryitems" + where(filter, true));
    bind_filter(statement, filter);
    std::vector<InventoryItem> result;
    while (statement.row()) {
        InventoryItem value;
        value.asset_id = Uuid(statement.text(0));
        value.asset_type = statement.integer(1);
        value.name = statement.text(2);
        value.description = statement.text(3);
        value.next_permissions = statement.integer(4);
        value.current_permissions = statement.integer(5);
        value.inventory_type = statement.integer(6);
        value.creator_id = statement.text(7);
        value.base_permissions = statement.integer(8);
        value.everyone_permissions = statement.integer(9);
        value.sale_price = statement.integer(10);
        value.sale_type = statement.integer(11);
        value.creation_date = statement.integer(12);
        value.group_id = Uuid(statement.text(13));
        value.group_owned = statement.integer(14);
        value.flags = statement.integer(15);
        value.id = Uuid(statement.text(16));
        value.owner = Uuid(statement.text(17));
        value.parent = Uuid(statement.text(18));
        value.group_permissions = statement.integer(19);
        result.push_back(std::move(value));
    }
    return result;
}
bool InventoryFolderStore::store(InventoryItem &item) {
    std::lock_guard lock(impl_->mutex);
    item.name = truncate_name(item.name);
    item.description = truncate_name(item.description, 128);
    Statement statement(impl_->db, "REPLACE INTO inventoryitems(" + item_column_list() +
                                       ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    statement.bind(1, item.asset_id.str());
    statement.bind(2, item.asset_type);
    statement.bind(3, item.name);
    statement.bind(4, item.description);
    statement.bind(5, item.next_permissions);
    statement.bind(6, item.current_permissions);
    statement.bind(7, item.inventory_type);
    statement.bind(8, item.creator_id);
    statement.bind(9, item.base_permissions);
    statement.bind(10, item.everyone_permissions);
    statement.bind(11, item.sale_price);
    statement.bind(12, item.sale_type);
    statement.bind(13, item.creation_date);
    statement.bind(14, item.group_id.str());
    statement.bind(15, item.group_owned);
    statement.bind(16, item.flags);
    statement.bind(17, item.id.str());
    statement.bind(18, item.owner.str());
    statement.bind(19, item.parent.str());
    statement.bind(20, item.group_permissions);
    statement.row();
    if (sqlite3_changes(impl_->db) == 0)
        return false;
    impl_->increment(item.parent);
    return true;
}
bool InventoryFolderStore::move_item(const Uuid &id, const Uuid &parent) {
    std::lock_guard lock(impl_->mutex);
    const auto previous = items({{"inventoryID", id.str()}});
    if (previous.empty())
        return false;
    Statement statement(impl_->db, "UPDATE inventoryitems SET parentFolderID=? WHERE inventoryID=?");
    statement.bind(1, parent.str());
    statement.bind(2, id.str());
    statement.row();
    if (sqlite3_changes(impl_->db) == 0)
        return false;
    impl_->increment(previous.front().parent);
    impl_->increment(parent);
    return true;
}
bool InventoryFolderStore::erase_items(const Filter &filter) {
    std::lock_guard lock(impl_->mutex);
    const auto previous = items(filter);
    if (previous.empty())
        return false;
    Statement statement(impl_->db, "DELETE FROM inventoryitems" + where(filter, true));
    bind_filter(statement, filter);
    statement.row();
    if (sqlite3_changes(impl_->db) == 0)
        return false;
    std::unordered_set<Uuid, framework::UuidHash> parents;
    for (const auto &item : previous)
        parents.insert(item.parent);
    for (const auto &parent : parents)
        impl_->increment(parent);
    return true;
}
std::vector<InventoryItem> InventoryFolderStore::active_gestures(const Uuid &owner) {
    return items({{"avatarID", owner.str()}, {"assetType", "21"}, {"flags", "1"}});
}
int InventoryFolderStore::asset_permissions(const Uuid &owner, const Uuid &asset) {
    std::lock_guard lock(impl_->mutex);
    Statement statement(
        impl_->db, "SELECT inventoryCurrentPermissions FROM inventoryitems WHERE avatarID=? AND assetID=?");
    statement.bind(1, owner.str());
    statement.bind(2, asset.str());
    int result = 0;
    while (statement.row())
        result |= statement.integer(0);
    return result;
}
InventoryFolder InventoryFolderService::create(const Uuid &owner, const Uuid &parent, int type,
                                               const std::string &name) {
    InventoryFolder folder{name, type, 1, Uuid::random(), owner, parent};
    store_.store(folder);
    return folder;
}
std::optional<InventoryFolder> InventoryFolderService::root(const Uuid &owner) {
    auto folders = store_.folders({{"agentID", owner.str()}, {"parentFolderID", Uuid{}.str()}});
    if (folders.empty())
        return std::nullopt;
    const auto found = std::find_if(folders.begin(), folders.end(),
                                    [](const auto &folder) { return folder.name == "My Inventory"; });
    return framework_folder(found == folders.end() ? folders.front() : *found);
}
std::optional<InventoryFolder> InventoryFolderService::for_type(const Uuid &owner, int type) {
    auto root_folder = root(owner);
    if (!root_folder)
        return std::nullopt;
    if (type == 8)
        return root_folder;
    auto folders = store_.folders({{"agentID", owner.str()},
                                   {"parentFolderID", root_folder->id.str()},
                                   {"type", std::to_string(type)}});
    return folders.empty() ? std::nullopt : std::optional{framework_folder(folders.front())};
}
std::optional<std::vector<InventoryFolder>> InventoryFolderService::skeleton(const Uuid &owner) {
    auto folders = store_.folders({{"agentID", owner.str()}});
    for (auto &folder : folders)
        folder = framework_folder(std::move(folder));
    return folders.empty() ? std::nullopt : std::optional{std::move(folders)};
}
bool InventoryFolderService::create_user_inventory(const Uuid &owner) {
    auto root_folder = root(owner);
    const bool created = !root_folder;
    if (!root_folder)
        root_folder = create(owner, {}, 8, "My Inventory");
    const auto system = store_.folders({{"agentID", owner.str()}, {"parentFolderID", root_folder->id.str()}});
    const std::pair<int, const char *> defaults[] = {
        {20, "Animations"},     {13, "Body Parts"}, {2, "Calling Cards"}, {5, "Clothing"},
        {46, "Current Outfit"}, {23, "Favorites"},  {21, "Gestures"},     {3, "Landmarks"},
        {16, "Lost And Found"}, {7, "Notecards"},   {6, "Objects"},       {15, "Photo Album"},
        {10, "Scripts"},        {1, "Sounds"},      {0, "Textures"},      {14, "Trash"},
        {56, "Settings"},       {57, "Materials"}};
    for (const auto &[type, name] : defaults) {
        // GetSystemFolders explicitly excludes type zero. Preserve its
        // resulting repeated Textures-folder creation on subsequent calls.
        if (std::none_of(system.begin(), system.end(),
                         [&](const auto &folder) { return folder.type > 0 && folder.type == type; })) {
            auto folder = create(owner, root_folder->id, type, name);
            if (type == 2) {
                folder = create(owner, folder.id, 2, "Friends");
                create(owner, folder.id, 2, "All");
            }
        }
    }
    return created;
}
}

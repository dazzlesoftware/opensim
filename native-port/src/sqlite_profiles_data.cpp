// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_profiles_data.hpp"
#include "opensim/services/sqlite_generic_table.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "profiles_migration_resource.hpp"
#include <sqlite3.h>
#include <variant>
#include <limits>
namespace opensim::services {
namespace {
using namespace framework;
using network::Value;
using Text = ProfilesData::Text;
template <class T> T& required(const std::shared_ptr<T>& p) {
    if (!p)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *p;
}
bool empty(const Text& text) {
    return !text || text->empty();
}
Text nonnull(const Text& text) {
    if (!text)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return text;
}
using Parameter = std::variant<Text, std::int32_t, bool>;
using Params = std::vector<std::pair<std::string, Parameter>>;
const char* newline() {
#ifdef _WIN32
    return "\r\n";
#else
    return "\n";
#endif
}
[[noreturn]] void sql_error(sqlite3* db, int code) {
    throw std::runtime_error(std::string(sqlite3_errstr(code & 255)) + newline() + sqlite3_errmsg(db));
}
class Row {
    sqlite3_stmt* statement_;
    SqliteRow row_;
    std::string type(int i) const {
        if (sqlite3_column_type(statement_, i) == SQLITE_NULL)
            return "DBNull";
        auto declared = sqlite3_column_decltype(statement_, i);
        std::string d = declared ? declared : "";
        if (auto at = d.find('('); at != d.npos)
            d.resize(at);
        for (auto& c : d)
            if (c >= 'a' && c <= 'z')
                c -= 32;
        if (d == "TEXT" || d == "CHAR" || d == "VARCHAR" || d == "STRING" || d == "NVARCHAR")
            return "String";
        if (d == "INT" || d == "INT32" || d == "MEDIUMINT")
            return "Int32";
        if (d == "BINARY" || d == "BLOB")
            return "Byte[]";
        if (d == "BOOLEAN" || d == "BOOL")
            return "Boolean";
        switch (sqlite3_column_type(statement_, i)) {
        case SQLITE_INTEGER:
            return "Int64";
        case SQLITE_FLOAT:
            return "Double";
        case SQLITE_BLOB:
            return "Byte[]";
        default:
            return "String";
        }
    }
    [[noreturn]] void bad_cast(int i, const char* target) const {
        throw std::runtime_error("Unable to cast object of type 'System." + type(i) + "' to type 'System." +
                                 target + "'.");
    }

  public:
    explicit Row(sqlite3_stmt* s) : statement_(s), row_(s) {
    }
    std::u16string text(const char* name) const {
        return utf16_replacing(row_.value_text(row_.column(name)));
    }
    std::u16string string(const char* name) const {
        int i = row_.column(name);
        if (type(i) != "String")
            bad_cast(i, "String");
        return utf16_replacing(row_.value_text(i));
    }
    std::int32_t integer(const char* name) const {
        int i = row_.column(name);
        if (type(i) != "Int32")
            bad_cast(i, "Int32");
        return sqlite3_column_int(statement_, i);
    }
    std::int32_t convert_integer(const char* name) const {
        int i = row_.column(name);
        if (type(i) == "DBNull")
            throw std::runtime_error("Object cannot be cast from DBNull to other types.");
        return invariant_convert_to_int32(text(name));
    }
    std::int32_t short_integer(const char* name) const {
        auto n = convert_integer(name);
        if (n < INT16_MIN || n > INT16_MAX)
            throw std::runtime_error("Value was either too large or too small for an Int16.");
        return n;
    }
    std::uint8_t flags(const char* name) const {
        auto raw = text(name);
        auto n = invariant_convert_to_int64(raw);
        if (n < 0 || n > UINT32_MAX)
            throw std::runtime_error("Value was either too large or too small for a UInt32.");
        return static_cast<std::uint8_t>(n);
    }
};
struct Command {
    std::shared_ptr<SqliteDatabase> db;
    std::unique_lock<std::recursive_mutex> lock;
    sqlite3_stmt* statement = nullptr;
    bool retain = false;
    Command(const std::shared_ptr<SqliteDatabase>& database, const std::string& sql, const Params& params)
        : db(database), lock(required(db).mutex()) {
        if (!db->handle())
            throw std::runtime_error("Database is not open");
        try {
            int code = sqlite3_prepare_v2(db->handle(), sql.c_str(), -1, &statement, nullptr);
            if (code != SQLITE_OK)
                sql_error(db->handle(), code);
            if (!statement)
                throw std::runtime_error("Empty SQL statement");
            for (int i = 1; i <= sqlite3_bind_parameter_count(statement); ++i) {
                const char* key = sqlite3_bind_parameter_name(statement, i);
                auto p = std::find_if(params.begin(), params.end(),
                                      [&](auto& value) { return key && value.first == key; });
                if (p == params.end())
                    throw std::runtime_error(std::string("unknown error") + newline() +
                                             "Insufficient parameters supplied to the command");
                std::visit(
                    [&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        int result;
                        if constexpr (std::is_same_v<T, Text>) {
                            if (value) {
                                auto bytes = utf8(*value);
                                result = sqlite3_bind_text(statement, i, bytes.data(),
                                                           static_cast<int>(bytes.size()), SQLITE_TRANSIENT);
                            } else
                                result = sqlite3_bind_null(statement, i);
                        } else
                            result = sqlite3_bind_int(statement, i, static_cast<int>(value));
                        if (result != SQLITE_OK)
                            sql_error(db->handle(), result);
                    },
                    p->second);
            }
        } catch (...) {
            if (statement)
                sqlite3_finalize(statement);
            statement = nullptr;
            throw;
        }
    }
    ~Command() {
        if (statement) {
            if (retain)
                db->retain_reader(statement);
            else
                sqlite3_finalize(statement);
        }
    }
    bool read() {
        int result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
            return true;
        if (result == SQLITE_DONE)
            return false;
        sql_error(db->handle(), result);
    }
    Row row() const {
        return Row(statement);
    }
    void execute() {
        while (read()) {
        }
    }
};
void execute(const std::shared_ptr<SqliteDatabase>& db, const std::string& sql, Params params) {
    Command(db, sql, params).execute();
}
std::shared_ptr<Value> array() {
    auto v = std::make_shared<Value>();
    v->kind = Value::Kind::Array;
    return v;
}
Value string(std::u16string text) {
    Value v(utf8(text));
    v.sdk_string = std::make_shared<const std::u16string>(std::move(text));
    return v;
}
Value uuid(Uuid id) {
    Value value(id.str());
    value.kind = Value::Kind::Uuid;
    return value;
}
Uuid parsed(std::u16string_view value) {
    Uuid id;
    try_parse_sdk_uuid(value, id);
    return id;
}
bool boolean(std::u16string value) {
    while (!value.empty() && (value.front() == 0 || unicode_whitespace(value.front())))
        value.erase(value.begin());
    while (!value.empty() && (value.back() == 0 || unicode_whitespace(value.back())))
        value.pop_back();
    return ordinal_equals_ignore_case(utf8(value), "true");
}
Text id(Uuid value) {
    return utf16(value.str());
}
struct Resources : MigrationResources {
    std::vector<std::string> names() override {
        return {"OpenSim.Data.SQLite.Resources.UserProfiles.migrations"};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        return name == names()[0] ? std::optional<std::string>(ProfilesMigration) : std::nullopt;
    }
};
}
SqliteProfilesData::SqliteProfilesData(const std::string& connection, Logger logger, Clock clock)
    : logger_(std::move(logger)), clock_(std::move(clock)) {
    initialise(connection);
}
void SqliteProfilesData::initialise(const std::string& connection) {
    database_ = std::make_shared<SqliteDatabase>();
    database_->open(connection);
    Migration migration(sqlite_migration_database(database_->handle()), std::make_shared<Resources>(),
                        "UserProfiles");
    migration.update();
}
void SqliteProfilesData::error(std::string_view operation, const std::exception& e) const {
    if (logger_)
        logger_(u"[PROFILES_DATA]: " + utf16(operation) + u" exception " + utf16_replacing(e.what()));
}
std::shared_ptr<Value> SqliteProfilesData::GetClassifiedRecords(Uuid creator) {
    auto result = array();
    Command command(database_, "SELECT classifieduuid, name FROM classifieds WHERE creatoruuid = :Id",
                    {{":Id", id(creator)}});
    while (command.read()) {
        Uuid record;
        Text name;
        try {
            auto row = command.row();
            record = parsed(row.text("classifieduuid"));
            name = row.text("name");
        } catch (const std::exception& e) {
            error("UserAccount", e);
        }
        result->array.push_back(
            Value::object({{"classifieduuid", uuid(record)}, {"name", string(name.value_or(u""))}}));
    }
    return result;
}
bool SqliteProfilesData::UpdateClassifiedRecord(std::shared_ptr<UserClassifiedAdd> value, Text& result) {
    auto& ad = required(value);
    if (empty(ad.ParcelName))
        ad.ParcelName = u"Unknown";
    if (empty(ad.Description))
        ad.Description = u"No Description";
    auto now = clock_ ? clock_() : shared_native_invariant_datetime_context()->now();
    constexpr std::int64_t epoch = 621355968000000000LL;
    auto duration = static_cast<std::int64_t>(ad.Flags == 2 ? 7 : 365) * 864000000000LL;
    if (now.ticks() > 3155378975999999999LL - duration)
        throw std::out_of_range(
            "The added or subtracted value results in an un-representable DateTime. (Parameter 'value')");
    auto seconds = [](std::int64_t ticks) {
        double n = static_cast<double>(ticks) / 10000000.;
        return n < INT32_MIN || n > INT32_MAX ? INT32_MIN : static_cast<std::int32_t>(n);
    };
    ad.CreationDate = seconds(now.ticks() - epoch);
    ad.ExpirationDate = seconds(now.ticks() + duration - epoch);
    try {
        execute(
            database_,
            "INSERT OR REPLACE INTO classifieds (`classifieduuid`,`creatoruuid`,`creationdate`,`expirationdate`,`category`,`name`,`description`,`parceluuid`,`parentestate`,`snapshotuuid`,`simname`,`posglobal`,`parcelname`,`classifiedflags`,`priceforlisting`) VALUES (:ClassifiedId,:CreatorId,:CreatedDate,:ExpirationDate,:Category,:Name,:Description,:ParcelId,:ParentEstate,:SnapshotId,:SimName,:GlobalPos,:ParcelName,:Flags,:ListingPrice)",
            {{":ClassifiedId", id(ad.ClassifiedId)},
             {":CreatorId", id(ad.CreatorId)},
             {":CreatedDate", Text(utf16(std::to_string(ad.CreationDate)))},
             {":ExpirationDate", Text(utf16(std::to_string(ad.ExpirationDate)))},
             {":Category", Text(utf16(std::to_string(ad.Category)))},
             {":Name", nonnull(ad.Name)},
             {":Description", nonnull(ad.Description)},
             {":ParcelId", id(ad.ParcelId)},
             {":ParentEstate", Text(utf16(std::to_string(ad.ParentEstate)))},
             {":SnapshotId", id(ad.SnapshotId)},
             {":SimName", nonnull(ad.SimName)},
             {":GlobalPos", nonnull(ad.GlobalPos)},
             {":ParcelName", nonnull(ad.ParcelName)},
             {":Flags", Text(utf16(std::to_string(ad.Flags)))},
             {":ListingPrice", Text(utf16(std::to_string(ad.Price)))}});
    } catch (const std::exception& e) {
        error("ClassifiedesUpdate", e);
        result = utf16_replacing(e.what());
        return false;
    }
    return true;
}
bool SqliteProfilesData::DeleteClassifiedRecord(Uuid record) {
    try {
        execute(database_, "DELETE FROM classifieds WHERE classifieduuid = :ClasifiedId",
                {{":ClassifiedId", id(record)}});
    } catch (const std::exception& e) {
        error("DeleteClassifiedRecord", e);
        return false;
    }
    return true;
}
bool SqliteProfilesData::GetClassifiedInfo(std::shared_ptr<UserClassifiedAdd>& value, Text& result) {
    (void)result;
    try {
        Command command(database_, "SELECT * FROM classifieds WHERE classifieduuid = :AdId",
                        {{":AdId", id(required(value).ClassifiedId)}});
        if (command.read()) {
            auto row = command.row();
            auto& ad = required(value);
            ad.CreatorId = construct_sdk_uuid(row.text("creatoruuid"));
            ad.ParcelId = construct_sdk_uuid(row.text("parceluuid"));
            ad.SnapshotId = construct_sdk_uuid(row.text("snapshotuuid"));
            ad.CreationDate = row.convert_integer("creationdate");
            ad.ExpirationDate = row.convert_integer("expirationdate");
            ad.ParentEstate = row.convert_integer("parentestate");
            ad.Flags = row.flags("classifiedflags");
            ad.Category = row.convert_integer("category");
            ad.Price = row.short_integer("priceforlisting");
            ad.Name = row.text("name");
            ad.Description = row.text("description");
            ad.SimName = row.text("simname");
            ad.GlobalPos = row.text("posglobal");
            ad.ParcelName = row.text("parcelname");
        }
    } catch (const std::exception& e) {
        error("GetPickInfo", e);
    }
    return true;
}
std::shared_ptr<Value> SqliteProfilesData::GetAvatarPicks(Uuid creator) {
    auto result = array();
    try {
        Command command(database_, "SELECT `pickuuid`,`name` FROM userpicks WHERE creatoruuid = :Id",
                        {{":Id", id(creator)}});
        while (command.read()) {
            auto row = command.row();
            Value record;
            record.kind = Value::Kind::Map;
            record.set("pickuuid", string(row.string("pickuuid")));
            record.set("name", string(row.string("name")));
            result->array.push_back(std::move(record));
        }
    } catch (const std::exception& e) {
        error("GetAvatarPicks", e);
    }
    return result;
}
std::shared_ptr<UserProfilePick> SqliteProfilesData::GetPickInfo(Uuid avatar, Uuid pick_id) {
    auto pick = std::make_shared<UserProfilePick>();
    try {
        Command command(database_,
                        "SELECT * FROM userpicks WHERE creatoruuid = :CreatorId AND pickuuid = :PickId",
                        {{":CreatorId", id(avatar)}, {":PickId", id(pick_id)}});
        while (command.read()) {
            auto row = command.row();
            auto description = row.string("description");
            if (description.empty())
                description = u"No description given.";
            pick->PickId = parsed(row.string("pickuuid"));
            pick->CreatorId = parsed(row.string("creatoruuid"));
            pick->ParcelId = parsed(row.string("parceluuid"));
            pick->SnapshotId = parsed(row.string("snapshotuuid"));
            pick->GlobalPos = row.string("posglobal");
            pick->Gatekeeper = row.string("gatekeeper");
            pick->TopPick = boolean(row.text("toppick"));
            pick->Enabled = boolean(row.text("enabled"));
            pick->Name = row.string("name");
            pick->Desc = description;
            pick->ParcelName = row.string("user");
            pick->OriginalName = row.string("originalname");
            pick->SimName = row.string("simname");
            pick->SortOrder = row.integer("sortorder");
        }
    } catch (const std::exception& e) {
        error("GetPickInfo", e);
    }
    return pick;
}
bool SqliteProfilesData::UpdatePicksRecord(std::shared_ptr<UserProfilePick> value) {
    try {
        auto& pick = required(value);
        execute(
            database_,
            "INSERT OR REPLACE INTO userpicks (pickuuid,creatoruuid,toppick,parceluuid,name,description,snapshotuuid,user,originalname,simname,posglobal,gatekeeper,sortorder,enabled) VALUES (:PickId,:CreatorId,:TopPick,:ParcelId,:Name,:Desc,:SnapshotId,:User,:Original,:SimName,:GlobalPos,:Gatekeeper,:SortOrder,:Enabled)",
            {{":PickId", id(pick.PickId)},
             {":CreatorId", id(pick.CreatorId)},
             {":TopPick", 0},
             {":ParcelId", id(pick.ParcelId)},
             {":Name", nonnull(pick.Name)},
             {":Desc", nonnull(pick.Desc)},
             {":SnapshotId", id(pick.SnapshotId)},
             {":User", nonnull(pick.ParcelName)},
             {":Original", nonnull(pick.OriginalName)},
             {":SimName", nonnull(pick.SimName)},
             {":GlobalPos", pick.GlobalPos},
             {":Gatekeeper", pick.Gatekeeper},
             {":SortOrder", Text(utf16(std::to_string(pick.SortOrder)))},
             {":Enabled", 0}});
    } catch (const std::exception& e) {
        error("UpdateAvatarPicks", e);
        return false;
    }
    return true;
}
bool SqliteProfilesData::DeletePicksRecord(Uuid pick) {
    try {
        execute(database_, "DELETE FROM userpicks WHERE pickuuid = :PickId", {{":PickId", id(pick)}});
    } catch (const std::exception& e) {
        error("DeleteUserPickRecord", e);
        return false;
    }
    return true;
}
bool SqliteProfilesData::GetAvatarNotes(std::shared_ptr<UserProfileNotes>& value) {
    try {
        auto& notes = required(value);
        Command command(database_,
                        "SELECT `notes` FROM usernotes WHERE useruuid = :Id AND targetuuid = :TargetId",
                        {{":Id", id(notes.UserId)}, {":TargetId", id(notes.TargetId)}});
        if (command.read())
            notes.Notes = command.row().string("notes");
    } catch (const std::exception& e) {
        error("GetAvatarNotes", e);
    }
    return true;
}
bool SqliteProfilesData::UpdateAvatarNotes(std::shared_ptr<UserProfileNotes>& value, Text& result) {
    (void)result;
    bool remove = empty(required(value).Notes);
    try {
        auto& note = required(value);
        Params p = {{":TargetId", id(note.TargetId)}, {":UserId", id(note.UserId)}};
        if (!remove)
            p.emplace_back(":Notes", note.Notes);
        execute(database_,
                remove ? "DELETE FROM usernotes WHERE useruuid=:UserId AND targetuuid=:TargetId"
                       : "INSERT OR REPLACE INTO usernotes VALUES (:UserId,:TargetId,:Notes)",
                std::move(p));
    } catch (const std::exception& e) {
        error("UpdateAvatarNotes", e);
        return false;
    }
    return true;
}
bool SqliteProfilesData::GetAvatarProperties(std::shared_ptr<UserProfileProperties>& value, Text& result) {
    auto& props = required(value);
    std::unique_ptr<Command> command;
    try {
        command = std::make_unique<Command>(database_, "SELECT * FROM userprofile WHERE useruuid = :Id",
                                            Params{{":Id", id(props.UserId)}});
    } catch (const std::exception& e) {
        error("GetAvatarProperties", e);
        result = utf16_replacing(e.what());
        return false;
    }
    command->retain = true; // Original provider never closes this IDataReader.
    if (command->read()) {
        auto row = command->row();
        props.WebUrl = row.string("profileURL");
        props.ImageId = parsed(row.string("profileImage"));
        props.AboutText = row.string("profileAboutText");
        props.FirstLifeImageId = parsed(row.string("profileFirstImage"));
        props.FirstLifeText = row.string("profileFirstText");
        props.PartnerId = parsed(row.string("profilePartner"));
        props.WantToMask = row.integer("profileWantToMask");
        props.WantToText = row.string("profileWantToText");
        props.SkillsMask = row.integer("profileSkillsMask");
        props.SkillsText = row.string("profileSkillsText");
        props.Language = row.string("profileLanguages");
    } else {
        props.WebUrl = u"";
        props.ImageId = {};
        props.AboutText = u"";
        props.FirstLifeImageId = {};
        props.FirstLifeText = u"";
        props.PartnerId = {};
        props.WantToMask = 0;
        props.WantToText = u"";
        props.SkillsMask = 0;
        props.SkillsText = u"";
        props.Language = u"";
        props.PublishProfile = false;
        props.PublishMature = false;
        execute(
            database_,
            "INSERT INTO userprofile (useruuid,profilePartner,profileAllowPublish,profileMaturePublish,profileURL,profileWantToMask,profileWantToText,profileSkillsMask,profileSkillsText,profileLanguages,profileImage,profileAboutText,profileFirstImage,profileFirstText) VALUES (:userId,:profilePartner,:profileAllowPublish,:profileMaturePublish,:profileURL,:profileWantToMask,:profileWantToText,:profileSkillsMask,:profileSkillsText,:profileLanguages,:profileImage,:profileAboutText,:profileFirstImage,:profileFirstText)",
            {{":userId", id(props.UserId)},
             {":profilePartner", id(props.PartnerId)},
             {":profileAllowPublish", props.PublishProfile},
             {":profileMaturePublish", props.PublishMature},
             {":profileURL", props.WebUrl},
             {":profileWantToMask", props.WantToMask},
             {":profileWantToText", props.WantToText},
             {":profileSkillsMask", props.SkillsMask},
             {":profileSkillsText", props.SkillsText},
             {":profileLanguages", props.Language},
             {":profileImage", id(props.ImageId)},
             {":profileAboutText", props.AboutText},
             {":profileFirstImage", id(props.FirstLifeImageId)},
             {":profileFirstText", props.FirstLifeText}});
    }
    return true;
}
bool SqliteProfilesData::UpdateAvatarProperties(std::shared_ptr<UserProfileProperties>& value, Text& result) {
    (void)result;
    try {
        auto& p = required(value);
        execute(
            database_,
            "UPDATE userprofile SET profileURL=:profileURL, profileImage=:image, profileAboutText=:abouttext,profileFirstImage=:firstlifeimage,profileFirstText=:firstlifetext WHERE useruuid=:uuid",
            {{":profileURL", p.WebUrl},
             {":image", id(p.ImageId)},
             {":abouttext", p.AboutText},
             {":firstlifeimage", id(p.FirstLifeImageId)},
             {":firstlifetext", p.FirstLifeText},
             {":uuid", id(p.UserId)}});
    } catch (const std::exception& e) {
        error("AgentPropertiesUpdate", e);
        return false;
    }
    return true;
}
bool SqliteProfilesData::UpdateAvatarInterests(std::shared_ptr<UserProfileProperties> value, Text& result) {
    try {
        auto& p = required(value);
        execute(
            database_,
            "UPDATE userprofile SET profileWantToMask=:WantMask, profileWantToText=:WantText,profileSkillsMask=:SkillsMask,profileSkillsText=:SkillsText, profileLanguages=:Languages WHERE useruuid=:uuid",
            {{":WantMask", p.WantToMask},
             {":WantText", p.WantToText},
             {":SkillsMask", p.SkillsMask},
             {":SkillsText", p.SkillsText},
             {":Languages", p.Language},
             {":uuid", id(p.UserId)}});
    } catch (const std::exception& e) {
        error("AgentInterestsUpdate", e);
        result = utf16_replacing(e.what());
        return false;
    }
    return true;
}
bool SqliteProfilesData::UpdateUserPreferences(std::shared_ptr<UserPreferences>& value, Text& result) {
    try {
        auto& p = required(value);
        execute(
            database_,
            "UPDATE usersettings SET imviaemail=:ImViaEmail, visible=:Visible, email=:EMail WHERE useruuid=:uuid",
            {{":ImViaEmail", p.IMViaEmail},
             {":Visible", p.Visible},
             {":EMail", p.EMail},
             {":uuid", id(p.UserId)}});
    } catch (const std::exception& e) {
        error("AgentInterestsUpdate", e);
        result = utf16_replacing(e.what());
        return false;
    }
    return true;
}
bool SqliteProfilesData::GetUserPreferences(std::shared_ptr<UserPreferences>& value, Text& result) {
    try {
        Command command(database_, "SELECT imviaemail,visible,email FROM usersettings WHERE useruuid = :Id",
                        {{"?Id", id(required(value).UserId)}});
        if (command.read()) {
            auto row = command.row();
            value->IMViaEmail = boolean(row.string("imviaemail"));
            value->Visible = boolean(row.string("visible"));
            value->EMail = row.string("email");
        } else
            throw std::runtime_error("Empty SQL statement");
    } catch (const std::exception& e) {
        error("Get preferences", e);
        result = utf16_replacing(e.what());
        return false;
    }
    return true;
}
bool SqliteProfilesData::GetUserAppData(std::shared_ptr<UserAppData>& value, Text& result) {
    try {
        auto& p = required(value);
        Command command(database_, "SELECT * FROM `userdata` WHERE UserId = :Id AND TagId = :TagId",
                        {{":Id", nonnull(p.UserId)}, {":TagId", nonnull(p.TagId)}});
        if (command.read()) {
            auto row = command.row();
            p.DataKey = row.string("DataKey");
            p.DataVal = row.string("DataVal");
        } else
            throw std::runtime_error("Cannot set CommandText while a DataReader is active");
    } catch (const std::exception& e) {
        error("Requst application data", e);
        result = utf16_replacing(e.what());
        return false;
    }
    return true;
}
bool SqliteProfilesData::SetUserAppData(std::shared_ptr<UserAppData> value, Text& result) {
    (void)result;
    try {
        auto& p = required(value);
        execute(
            database_,
            "UPDATE userdata SET TagId = :TagId, DataKey = :DataKey, DataVal = :DataVal WHERE UserId = :UserId AND TagId = :TagId",
            {{":UserId", nonnull(p.UserId)},
             {":TagId", nonnull(p.TagId)},
             {":DataKey", nonnull(p.DataKey)},
             {":DataVal", nonnull(p.DataKey)}});
    } catch (const std::exception& e) {
        error("SetUserData", e);
        return false;
    }
    return true;
}
std::shared_ptr<Value> SqliteProfilesData::GetUserImageAssets(Uuid creator) {
    auto result = array();
    try {
        for (const auto* table : {"classifieds", "userpicks"}) {
            Command command(database_,
                            std::string("SELECT `snapshotuuid` FROM \"") + table +
                                "\" WHERE `creatoruuid` = :Id",
                            {{":Id", id(creator)}});
            if (command.read())
                result->array.push_back(string(command.row().text("snapshotuuid")));
        }
        Command command(
            database_, "SELECT `profileImage`, `profileFirstImage` FROM `userprofile` WHERE `useruuid` = :Id",
            {{":Id", id(creator)}});
        if (command.read()) {
            auto row = command.row();
            result->array.push_back(string(row.text("profileImage")));
            result->array.push_back(string(row.text("profileFirstImage")));
        }
    } catch (const std::exception& e) {
        error("GetAvatarNotes", e);
    }
    return result;
}
}

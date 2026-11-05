// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_profiles_data.hpp"
#include "opensim/services/sqlite_generic_table.hpp"
#include "opensim/network/osd_json.hpp"
#include <sqlite3.h>
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace framework;
using namespace network;
using namespace services;
std::string H(std::string_view s) {
    std::string r;
    for (unsigned char c : s) {
        r += "0123456789abcdef"[c >> 4];
        r += "0123456789abcdef"[c & 15];
    }
    return r;
}
std::string U(std::string_view s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); i += 2)
        r += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return r;
}
std::string snap(const std::shared_ptr<Value>& v) {
    return v ? H(osd_json(*v, true)) : "N";
}
template <class T> void nulls(const std::shared_ptr<T>& value, const Value& data) {
    if (!data.contains("NullFields"))
        return;
    for (auto& field : data.get("NullFields").view().array) {
        auto name = osd_string(field);
        if constexpr (requires { value->AboutText.reset(); }) {
            if (name == "AboutText")
                value->AboutText.reset();
        }
        if constexpr (requires { value->DataKey.reset(); }) {
            if (name == "DataKey")
                value->DataKey.reset();
        }
        if constexpr (requires { value->DataVal.reset(); }) {
            if (name == "DataVal")
                value->DataVal.reset();
        }
        if constexpr (requires { value->Desc.reset(); }) {
            if (name == "Desc")
                value->Desc.reset();
        }
        if constexpr (requires { value->Description.reset(); }) {
            if (name == "Description")
                value->Description.reset();
        }
        if constexpr (requires { value->EMail.reset(); }) {
            if (name == "EMail")
                value->EMail.reset();
        }
        if constexpr (requires { value->EmailAddress.reset(); }) {
            if (name == "EmailAddress")
                value->EmailAddress.reset();
        }
        if constexpr (requires { value->FirstLifeText.reset(); }) {
            if (name == "FirstLifeText")
                value->FirstLifeText.reset();
        }
        if constexpr (requires { value->Firstname.reset(); }) {
            if (name == "Firstname")
                value->Firstname.reset();
        }
        if constexpr (requires { value->Gatekeeper.reset(); }) {
            if (name == "Gatekeeper")
                value->Gatekeeper.reset();
        }
        if constexpr (requires { value->GlobalPos.reset(); }) {
            if (name == "GlobalPos")
                value->GlobalPos.reset();
        }
        if constexpr (requires { value->Language.reset(); }) {
            if (name == "Language")
                value->Language.reset();
        }
        if constexpr (requires { value->LastName.reset(); }) {
            if (name == "LastName")
                value->LastName.reset();
        }
        if constexpr (requires { value->Name.reset(); }) {
            if (name == "Name")
                value->Name.reset();
        }
        if constexpr (requires { value->Notes.reset(); }) {
            if (name == "Notes")
                value->Notes.reset();
        }
        if constexpr (requires { value->OriginalName.reset(); }) {
            if (name == "OriginalName")
                value->OriginalName.reset();
        }
        if constexpr (requires { value->ParcelName.reset(); }) {
            if (name == "ParcelName")
                value->ParcelName.reset();
        }
        if constexpr (requires { value->Password.reset(); }) {
            if (name == "Password")
                value->Password.reset();
        }
        if constexpr (requires { value->SimName.reset(); }) {
            if (name == "SimName")
                value->SimName.reset();
        }
        if constexpr (requires { value->SkillsText.reset(); }) {
            if (name == "SkillsText")
                value->SkillsText.reset();
        }
        if constexpr (requires { value->TagId.reset(); }) {
            if (name == "TagId")
                value->TagId.reset();
        }
        if constexpr (requires { value->UserId.reset(); }) {
            if (name == "UserId")
                value->UserId.reset();
        }
        if constexpr (requires { value->WantToText.reset(); }) {
            if (name == "WantToText")
                value->WantToText.reset();
        }
        if constexpr (requires { value->WebUrl.reset(); }) {
            if (name == "WebUrl")
                value->WebUrl.reset();
        }
    }
}
int main(int argc, char** argv) {
    std::string log, line;
    SqliteProfilesData store(argc > 1 ? argv[1] : "Data Source=:memory:",
                             [&](auto text) { log += utf8(text) + "\n"; });
    auto db = store.database()->handle();
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op, raw;
        in >> op;
        log.clear();
        try {
            if (op == "G") {
                store.database()->collect_readers();
                std::cout << "G\n";
                continue;
            }
            if (op != "M") {
                in >> raw;
                auto sql = U(raw);
                if (op == "X") {
                    char* error = nullptr;
                    int code = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
                    if (code != SQLITE_OK) {
                        std::string message = std::string(sqlite3_errstr(code & 255)) +
#ifdef _WIN32
                                              "\r\n"
#else
                                              "\n"
#endif
                                              + (error ? error : sqlite3_errmsg(db));
                        sqlite3_free(error);
                        throw std::runtime_error(message);
                    }
                    std::cout << "X\n";
                } else {
                    sqlite3_stmt* statement = nullptr;
                    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
                        throw std::runtime_error(sqlite3_errmsg(db));
                    Value rows;
                    rows.kind = Value::Kind::Array;
                    while (sqlite3_step(statement) == SQLITE_ROW) {
                        Value row;
                        row.kind = Value::Kind::Array;
                        SqliteRow view(statement);
                        for (int i = 0; i < sqlite3_column_count(statement); ++i)
                            row.array.emplace_back(view.value_text(i));
                        rows.array.push_back(std::move(row));
                    }
                    sqlite3_finalize(statement);
                    std::cout << "Q|" << H(osd_json(rows, true)) << "\n";
                }
                continue;
            }
            int method;
            in >> method >> raw;
            std::optional<Value> values;
            if (raw != "-")
                values = parse_osd_json(U(raw));
            ProfilesData::Text result = u"initial";
            std::string status, snapshot = "-";
            switch (method) {
            case 0:
                status = "O";
                snapshot =
                    snap(store.GetClassifiedRecords(values ? osd_uuid(values->get("QueryId")) : Uuid{}));
                break;
            case 1: {
                auto value =
                    values ? std::make_shared<UserClassifiedAdd>() : std::shared_ptr<UserClassifiedAdd>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.UpdateClassifiedRecord(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 2: {
                auto value =
                    values ? std::make_shared<UserClassifiedAdd>() : std::shared_ptr<UserClassifiedAdd>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.GetClassifiedInfo(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 3:
                status = store.DeleteClassifiedRecord(values ? osd_uuid(values->get("QueryId")) : Uuid{})
                             ? "1"
                             : "0";
                break;
            case 4:
                status = "O";
                snapshot = snap(store.GetAvatarPicks(values ? osd_uuid(values->get("QueryId")) : Uuid{}));
                break;
            case 5: {
                auto value = store.GetPickInfo(values ? osd_uuid(values->get("QueryId")) : Uuid{},
                                               values ? osd_uuid(values->get("PickId")) : Uuid{});
                status = "O";
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 6: {
                auto value =
                    values ? std::make_shared<UserProfilePick>() : std::shared_ptr<UserProfilePick>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.UpdatePicksRecord(value) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 7:
                status =
                    store.DeletePicksRecord(values ? osd_uuid(values->get("QueryId")) : Uuid{}) ? "1" : "0";
                break;
            case 8: {
                auto value =
                    values ? std::make_shared<UserProfileNotes>() : std::shared_ptr<UserProfileNotes>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.GetAvatarNotes(value) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 9: {
                auto value =
                    values ? std::make_shared<UserProfileNotes>() : std::shared_ptr<UserProfileNotes>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.UpdateAvatarNotes(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 10: {
                auto value = values ? std::make_shared<UserProfileProperties>()
                                    : std::shared_ptr<UserProfileProperties>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.GetAvatarProperties(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 11: {
                auto value = values ? std::make_shared<UserProfileProperties>()
                                    : std::shared_ptr<UserProfileProperties>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.UpdateAvatarProperties(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 12: {
                auto value = values ? std::make_shared<UserProfileProperties>()
                                    : std::shared_ptr<UserProfileProperties>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.UpdateAvatarInterests(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 13: {
                auto value =
                    values ? std::make_shared<UserPreferences>() : std::shared_ptr<UserPreferences>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.UpdateUserPreferences(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 14: {
                auto value =
                    values ? std::make_shared<UserPreferences>() : std::shared_ptr<UserPreferences>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.GetUserPreferences(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 15:
                status = "O";
                snapshot = snap(store.GetUserImageAssets(values ? osd_uuid(values->get("QueryId")) : Uuid{}));
                break;
            case 16: {
                auto value = values ? std::make_shared<UserAppData>() : std::shared_ptr<UserAppData>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.GetUserAppData(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            case 17: {
                auto value = values ? std::make_shared<UserAppData>() : std::shared_ptr<UserAppData>{};
                if (value) {
                    value->deserialize_members(*values);
                    nulls(value, *values);
                }
                try {
                    status = store.SetUserAppData(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
                break;
            }
            }
            std::cout << status << "|" << (result ? H(utf8(*result)) : "N") << "|" << snapshot << "|"
                      << H(log) << "\n";
        } catch (const std::exception& e) {
            std::cout << "EX:" << H(e.what()) << "|" << H(log) << "\n";
        }
    }
}

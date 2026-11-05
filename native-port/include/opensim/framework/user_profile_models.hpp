// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/json_rpc_request.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/uuid.hpp"
namespace opensim::framework {
enum class JsonRpcFieldKind { Uuid, Integer, Byte, Boolean, String, Other };
struct JsonRpcFieldSchema {
    std::u16string name;
    JsonRpcFieldKind kind;
};
struct JsonRpcBoundField {
    std::u16string name;
    JsonRpcFieldKind kind;
    std::function<network::Value()> get;
    std::function<void(const network::Value&)> set;
    bool ignored = false;
};
class BoundJsonRpcMembers : public server::JsonRpcMembers {
  protected:
    virtual std::vector<JsonRpcBoundField> fields() = 0;
    static JsonRpcBoundField bind(std::u16string name, Uuid& field) {
        return {std::move(name), JsonRpcFieldKind::Uuid,
                [&field] {
                    network::Value value;
                    value.kind = network::Value::Kind::Uuid;
                    value.text = field.str();
                    return value;
                },
                [&field](const auto& value) { field = network::osd_uuid(value); }};
    }
    static JsonRpcBoundField bind(std::u16string name, std::int32_t& field) {
        return {std::move(name), JsonRpcFieldKind::Integer, [&field] { return network::Value(field); },
                [&field](const auto& value) { field = network::osd_integer(value); }};
    }
    static JsonRpcBoundField bind(std::u16string name, std::uint8_t& field) {
        return {
            std::move(name), JsonRpcFieldKind::Byte,
            [&field] { return network::Value(static_cast<std::int32_t>(field)); },
            [&field](const auto& value) { field = static_cast<std::uint8_t>(network::osd_integer(value)); }};
    }
    static JsonRpcBoundField bind(std::u16string name, bool& field) {
        return {std::move(name), JsonRpcFieldKind::Boolean, [&field] { return network::Value(field); },
                [&field](const auto& value) { field = network::osd_boolean(value); }};
    }
    static JsonRpcBoundField bind(std::u16string name, std::optional<std::u16string>& field) {
        return {std::move(name), JsonRpcFieldKind::String,
                [&field] {
                    if (!field)
                        return network::Value{};
                    network::Value value(utf8(*field));
                    value.sdk_string = std::make_shared<const std::u16string>(*field);
                    return value;
                },
                [&field](const auto& value) { field = network::osd_string_utf16(value); }};
    }

  public:
    std::vector<JsonRpcFieldSchema> schema() {
        std::vector<JsonRpcFieldSchema> result;
        for (auto& field : fields())
            result.push_back({field.name, field.kind});
        return result;
    }
    network::Value serialize_members() override {
        network::Value result;
        result.kind = network::Value::Kind::Map;
        for (auto& field : fields())
            if (!field.ignored) {
                auto value = field.get();
                if (value.view().kind != network::Value::Kind::Undefined ||
                    field.kind == JsonRpcFieldKind::String) {
                    auto name = network::sdk_map_key_bytes(field.name);
                    if (result.contains(name))
                        throw std::invalid_argument(
                            "An item with the same key has already been added. Key: " + utf8(field.name));
                    result.set(std::move(name), std::move(value));
                }
            }
        return result;
    }
    void deserialize_members(const network::Value& data) override {
        for (auto& field : fields())
            if (!field.ignored) {
                auto name = network::sdk_map_key_bytes(field.name);
                if (data.contains(name))
                    field.set(data.get(name));
            }
    }
};
struct UserClassifiedAdd : BoundJsonRpcMembers {
    Uuid ClassifiedId{};
    Uuid CreatorId{};
    std::int32_t CreationDate{};
    std::int32_t ExpirationDate{};
    std::int32_t Category{};
    std::optional<std::u16string> Name = std::u16string(u"");
    std::optional<std::u16string> Description = std::u16string(u"");
    Uuid ParcelId{};
    std::int32_t ParentEstate = 0;
    Uuid SnapshotId{};
    std::optional<std::u16string> SimName = std::u16string(u"");
    std::optional<std::u16string> GlobalPos = std::u16string(u"<0,0,0>");
    std::optional<std::u16string> ParcelName = std::u16string(u"");
    std::uint8_t Flags = 0;
    std::int32_t Price = 0;

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"ClassifiedId", ClassifiedId),
                bind(u"CreatorId", CreatorId),
                bind(u"CreationDate", CreationDate),
                bind(u"ExpirationDate", ExpirationDate),
                bind(u"Category", Category),
                bind(u"Name", Name),
                bind(u"Description", Description),
                bind(u"ParcelId", ParcelId),
                bind(u"ParentEstate", ParentEstate),
                bind(u"SnapshotId", SnapshotId),
                bind(u"SimName", SimName),
                bind(u"GlobalPos", GlobalPos),
                bind(u"ParcelName", ParcelName),
                bind(u"Flags", Flags),
                bind(u"Price", Price)};
    }
};
struct UserProfileProperties : BoundJsonRpcMembers {
    Uuid UserId{};
    Uuid PartnerId{};
    bool PublishProfile{};
    bool PublishMature{};
    std::optional<std::u16string> WebUrl = std::u16string(u"");
    std::int32_t WantToMask = 0;
    std::optional<std::u16string> WantToText = std::u16string(u"");
    std::int32_t SkillsMask = 0;
    std::optional<std::u16string> SkillsText = std::u16string(u"");
    std::optional<std::u16string> Language = std::u16string(u"");
    Uuid ImageId{};
    std::optional<std::u16string> AboutText = std::u16string(u"");
    Uuid FirstLifeImageId{};
    std::optional<std::u16string> FirstLifeText = std::u16string(u"");

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"UserId", UserId),
                bind(u"PartnerId", PartnerId),
                bind(u"PublishProfile", PublishProfile),
                bind(u"PublishMature", PublishMature),
                bind(u"WebUrl", WebUrl),
                bind(u"WantToMask", WantToMask),
                bind(u"WantToText", WantToText),
                bind(u"SkillsMask", SkillsMask),
                bind(u"SkillsText", SkillsText),
                bind(u"Language", Language),
                bind(u"ImageId", ImageId),
                bind(u"AboutText", AboutText),
                bind(u"FirstLifeImageId", FirstLifeImageId),
                bind(u"FirstLifeText", FirstLifeText)};
    }
};
struct UserProfilePick : BoundJsonRpcMembers {
    Uuid PickId{};
    Uuid CreatorId{};
    bool TopPick{};
    std::optional<std::u16string> Name = std::u16string(u"");
    std::optional<std::u16string> OriginalName = std::u16string(u"");
    std::optional<std::u16string> Desc = std::u16string(u"");
    Uuid ParcelId{};
    Uuid SnapshotId{};
    std::optional<std::u16string> ParcelName = std::u16string(u"");
    std::optional<std::u16string> SimName = std::u16string(u"");
    std::optional<std::u16string> GlobalPos = std::u16string(u"<0,0,0>");
    std::optional<std::u16string> Gatekeeper = std::u16string(u"");
    std::int32_t SortOrder = 0;
    bool Enabled = false;

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"PickId", PickId),
                bind(u"CreatorId", CreatorId),
                bind(u"TopPick", TopPick),
                bind(u"Name", Name),
                bind(u"OriginalName", OriginalName),
                bind(u"Desc", Desc),
                bind(u"ParcelId", ParcelId),
                bind(u"SnapshotId", SnapshotId),
                bind(u"ParcelName", ParcelName),
                bind(u"SimName", SimName),
                bind(u"GlobalPos", GlobalPos),
                bind(u"Gatekeeper", Gatekeeper),
                bind(u"SortOrder", SortOrder),
                bind(u"Enabled", Enabled)};
    }
};
struct UserProfileNotes : BoundJsonRpcMembers {
    Uuid UserId{};
    Uuid TargetId{};
    std::optional<std::u16string> Notes{};

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"UserId", UserId), bind(u"TargetId", TargetId), bind(u"Notes", Notes)};
    }
};
struct UserPreferences : BoundJsonRpcMembers {
    Uuid UserId{};
    bool IMViaEmail{};
    bool Visible{};
    std::optional<std::u16string> EMail = std::u16string(u"");

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"UserId", UserId), bind(u"IMViaEmail", IMViaEmail), bind(u"Visible", Visible),
                bind(u"EMail", EMail)};
    }
};
struct UserAccountProperties : BoundJsonRpcMembers {
    std::optional<std::u16string> EmailAddress = std::u16string(u"");
    std::optional<std::u16string> Firstname = std::u16string(u"");
    std::optional<std::u16string> LastName = std::u16string(u"");
    std::optional<std::u16string> Password = std::u16string(u"");
    std::optional<std::u16string> UserId = std::u16string(u"");

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"EmailAddress", EmailAddress), bind(u"Firstname", Firstname),
                bind(u"LastName", LastName), bind(u"Password", Password), bind(u"UserId", UserId)};
    }
};
struct UserAccountAuth : BoundJsonRpcMembers {
    std::optional<std::u16string> UserId = std::u16string(u"00000000-0000-0000-0000-000000000000");
    std::optional<std::u16string> Password = std::u16string(u"");

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"UserId", UserId), bind(u"Password", Password)};
    }
};
struct UserAppData : BoundJsonRpcMembers {
    std::optional<std::u16string> TagId = std::u16string(u"");
    std::optional<std::u16string> DataKey = std::u16string(u"");
    std::optional<std::u16string> UserId = std::u16string(u"00000000-0000-0000-0000-000000000000");
    std::optional<std::u16string> DataVal = std::u16string(u"");

  protected:
    std::vector<JsonRpcBoundField> fields() override {
        return {bind(u"TagId", TagId), bind(u"DataKey", DataKey), bind(u"UserId", UserId),
                bind(u"DataVal", DataVal)};
    }
};
}

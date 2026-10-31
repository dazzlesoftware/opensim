// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/asset_types.hpp"
#include <memory>
#include <optional>
#include <vector>
namespace opensim::framework {
// Models CLR byte[]: published storage is a fixed-length array with mutable elements.
using AssetBytes=std::shared_ptr<std::vector<std::uint8_t>>;
enum AssetFlags:std::int32_t{Normal=0,Maptile=1,Rewritable=2,Collectable=4};
class AssetDateTime {
public:
    AssetDateTime()=default;
    AssetDateTime(std::int64_t ticks,int kind);
    std::int64_t ticks()const{return ticks_;}int kind()const{return kind_;}
    static AssetDateTime from_serialized(std::uint64_t bits);
    std::uint64_t serialized_data()const{return static_cast<std::uint64_t>(ticks_)|(static_cast<std::uint64_t>(kind_bits_)<<62);}
private:
    std::int64_t ticks_=0;int kind_=0;std::uint8_t kind_bits_=0;
};
class AssetMetadata {
public:
    AssetMetadata()=default;
    AssetMetadata(const AssetMetadata&)=delete;
    AssetMetadata& operator=(const AssetMetadata&)=delete;
    Uuid full_id()const{return full_id_;}
    void full_id(const Uuid& value){full_id_=value;id_=value.str();}
    std::string id()const;
    void id(const std::optional<std::string>& value);
    std::optional<std::string> name=std::string{},description=std::string{},creator_id;
    AssetDateTime creation_date;
    std::int8_t type=-1;
    AssetBytes sha1;
    bool local=false,temporary=false;
    std::int32_t flags=Normal;
    std::string content_type()const;
    void content_type(const std::optional<std::string>& value);
    const std::optional<std::string>& raw_id()const{return id_;}
    const std::optional<std::string>& raw_content_type()const{return content_type_;}
private:
    friend struct AssetNrbfCodec;
    Uuid full_id_;
    mutable std::optional<std::string> id_;
    std::optional<std::string> content_type_;
};
class AssetBase {
public:
    static constexpr int MaxAssetName=64,MaxAssetDescription=64;
    AssetBase();
    AssetBase(const Uuid& id,const std::optional<std::string>& name,std::int8_t type,const std::optional<std::string>& creator);
    AssetBase(const std::optional<std::string>& id,const std::optional<std::string>& name,std::int8_t type,const std::optional<std::string>& creator);
    AssetBase(const AssetBase&)=delete;
    AssetBase& operator=(const AssetBase&)=delete;
    AssetBytes data;
    int upload_attempts=0;
    std::shared_ptr<AssetMetadata> metadata()const{return metadata_;}
    void metadata(std::shared_ptr<AssetMetadata> value){metadata_=std::move(value);}
    Uuid full_id()const{return required().full_id();}void full_id(const Uuid& value){required().full_id(value);}
    std::string id()const{return required().id();}void id(const std::optional<std::string>& value){required().id(value);}
    std::optional<std::string> name()const{return required().name;}void name(const std::optional<std::string>& value){required().name=value;}
    std::optional<std::string> description()const{return required().description;}void description(const std::optional<std::string>& value){required().description=value;}
    std::optional<std::string> creator_id()const{return required().creator_id;}void creator_id(const std::optional<std::string>& value){required().creator_id=value;}
    std::int8_t type()const{return required().type;}void type(std::int8_t value){required().type=value;}
    bool local()const{return required().local;}void local(bool value){required().local=value;}
    bool temporary()const{return required().temporary;}void temporary(bool value){required().temporary=value;}
    std::int32_t flags()const{return required().flags;}void flags(std::int32_t value){required().flags=value;}
    bool is_binary()const;bool is_textual()const{return !is_binary();}bool contains_references()const;
    std::string to_string()const{return full_id().str();}
private:
    AssetMetadata& required()const;
    std::shared_ptr<AssetMetadata> metadata_;
};
}

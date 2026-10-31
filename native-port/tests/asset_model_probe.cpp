// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/asset_base.hpp"
#include <bit>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim::framework;
Uuid id(int n){std::ostringstream out;out<<"00000000-0000-0000-0000-"<<std::hex<<std::setw(12)<<std::setfill('0')<<n;return Uuid(out.str());}
std::string unhex(const std::string& value){std::string result;for(std::size_t i=0;i<value.size();i+=2)result+=static_cast<char>(std::stoi(value.substr(i,2),nullptr,16));return result;}
std::string hex(std::string_view value){constexpr char digits[]="0123456789abcdef";std::string result;for(auto c:value){const auto byte=static_cast<unsigned char>(c);result+=digits[byte>>4];result+=digits[byte&15];}return result;}
std::optional<std::string> text(const std::string& value){if(value=="-")return {};return value=="."?"":unhex(value);}
std::string encode(const std::optional<std::string>& value){return value?"s"+hex(*value):"n";}
std::string bytes(const AssetBytes& data){return data?"b"+hex({reinterpret_cast<const char*>(data->data()),data->size()}):"n";}
AssetMetadata& required(const std::shared_ptr<AssetBase>& asset){auto metadata=asset->metadata();if(!metadata)throw std::runtime_error("null metadata");return *metadata;}
std::string snapshot(const std::shared_ptr<AssetBase>& a){
    std::string result=bytes(a->data)+"|"+std::to_string(a->upload_attempts)+"|";
    try{const auto& m=required(a);result+=a->full_id().str()+"|"+encode(a->id())+"|"+encode(a->name())+"|"+encode(a->description())+"|"+encode(a->creator_id())+"|"+std::to_string(a->type())+"|"+std::to_string(a->local())+"|"+std::to_string(a->temporary())+"|"+std::to_string(a->flags())+"|"+encode(m.content_type())+"|"+std::to_string(m.creation_date.ticks())+":"+std::to_string(m.creation_date.kind())+"|"+bytes(m.sha1)+"|"+std::to_string(a->is_binary())+std::to_string(a->is_textual())+std::to_string(a->contains_references())+"|"+a->to_string();}catch(const std::exception&){result+="METAEX";}return result;
}
int main(){auto asset=std::make_shared<AssetBase>();std::string line;while(std::getline(std::cin,line)){std::istringstream input(line);std::string command,arg;input>>command;std::string prefix;
    try{
        if(command=="default")asset=std::make_shared<AssetBase>();
        else if(command=="metadata-default")asset->metadata(std::make_shared<AssetMetadata>());
        else if(command=="metadata-null")asset->metadata(nullptr);
        else if(command=="uuid"||command=="string"){std::string name,creator;int type;input>>arg>>type>>name>>creator;const auto code=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(type));if(command=="uuid")asset=std::make_shared<AssetBase>(id(std::stoi(arg)),text(name),code,text(creator));else asset=std::make_shared<AssetBase>(text(arg),text(name),code,text(creator));}
        else if(command=="full"){int value;input>>value;asset->full_id(id(value));}
        else if(command=="id"){input>>arg;asset->id(text(arg));}
        else if(command=="name"){input>>arg;asset->name(text(arg));}
        else if(command=="description"){input>>arg;asset->description(text(arg));}
        else if(command=="creator"){input>>arg;asset->creator_id(text(arg));}
        else if(command=="content"){input>>arg;required(asset).content_type(text(arg));}
        else if(command=="type"){int value;input>>value;asset->type(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value)));}
        else if(command=="flags"){std::int32_t value;input>>value;asset->flags(value);}
        else if(command=="upload"){input>>asset->upload_attempts;}
        else if(command=="local"||command=="temporary"){int value;input>>value;if(command=="local")asset->local(value!=0);else asset->temporary(value!=0);}
        else if(command=="date"){std::int64_t ticks;int kind;input>>ticks>>kind;required(asset).creation_date=AssetDateTime(ticks,kind);}
        else if(command=="data"||command=="sha"){input>>arg;AssetBytes data;if(arg!="-"){const auto decoded=text(arg).value();data=std::make_shared<std::vector<std::uint8_t>>(decoded.begin(),decoded.end());}if(command=="data")asset->data=data;else required(asset).sha1=data;}
        else if(command=="share"){auto other=std::make_shared<AssetBase>();other->metadata(asset->metadata());other->data=asset->data;other->name("shared");other->type(56);if(other->data&&!other->data->empty())other->data->front()^=255;auto sha=required(other).sha1;if(sha&&!sha->empty())sha->back()^=255;}
        else if(command=="code"){int value;input>>value;prefix="CODE:"+std::to_string(static_cast<int>(asset_type_from_code(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value)))))+":"+asset_content_type(value)+":"+inventory_content_type(value)+":"+asset_extension(value)+"|";}
        else if(command=="lookup"){input>>arg;prefix="LOOKUP:"+std::to_string(content_asset_type(text(arg)))+":"+std::to_string(content_inventory_type(text(arg)))+"|";}
        else if(command=="names"){input>>arg;prefix="NAMES:"+std::to_string(asset_name_type(text(arg)))+":"+std::to_string(inventory_name_type(text(arg)))+"|";}
        else if(command=="limits")prefix="LIMITS:"+std::to_string(AssetBase::MaxAssetName)+":"+std::to_string(AssetBase::MaxAssetDescription)+":"+std::to_string(Normal)+":"+std::to_string(Maptile)+":"+std::to_string(Rewritable)+":"+std::to_string(Collectable)+"|";
        else throw std::runtime_error("unknown asset model command");
    }catch(const std::exception&){prefix="EX|";}
    std::cout<<prefix<<snapshot(asset)<<'\n';
}return 0;}

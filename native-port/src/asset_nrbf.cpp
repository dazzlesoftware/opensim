// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/asset_nrbf.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <tuple>
#include <stdexcept>
namespace opensim::framework {
namespace {
[[noreturn]]void bad(){throw std::invalid_argument("Invalid or unsupported AssetBase NRBF graph");}
// Decimal values can occur in ignored legacy members. Preserve the original
// decimal.Parse validation rather than rejecting every such graph.
void decimal(std::string value){
 auto white=[](char c){return c==' '||(c>='\t'&&c<='\r');};
 while(!value.empty()&&value.back()==0)value.pop_back();
 std::size_t start=0,end=value.size();while(start<end&&white(value[start]))++start;while(end>start&&white(value[end-1]))--end;
 bool sign=false;if(start<end&&(value[start]=='+'||value[start]=='-')){sign=true;++start;}
 std::string digits;std::size_t scale=0;bool point=false,any=false;
 while(start<end){char c=value[start];if(c>='0'&&c<='9'){digits+=c;any=true;if(point)++scale;++start;}else if(c=='.'&&!point){point=true;++start;}else if(c==','&&!point&&any)++start;else break;}
 if(!any)bad();while(start<end&&white(value[start]))++start;if(start<end&&(value[start]=='+'||value[start]=='-')&&!sign){++start;while(start<end&&white(value[start]))++start;}if(start!=end)bad();
 auto nonzero=digits.find_first_not_of('0');digits=nonzero==digits.npos?"0":digits.substr(nonzero);
 const std::string maximum="79228162514264337593543950335";auto target=std::min<std::size_t>(28,scale);
 for(;;){auto drop=scale-target;std::string rounded=drop>=digits.size()?"0":digits.substr(0,digits.size()-drop);bool up=false;
  if(drop&&drop<=digits.size()){auto at=digits.size()-drop;char c=digits[at];up=c>'5'||(c=='5'&&(digits.find_first_not_of('0',at+1)!=digits.npos||((rounded.back()-'0')&1)));}
  if(up){std::size_t i=rounded.size();while(i&&rounded[i-1]=='9'){rounded[--i]='0';}if(i)++rounded[i-1];else rounded.insert(rounded.begin(),'1');}
  if(rounded.size()<maximum.size()||(rounded.size()==maximum.size()&&rounded<=maximum))return;if(!target)bad();--target;
 }
}
struct Node;using N=std::shared_ptr<Node>;
struct Node {int kind=0;std::uint64_t bits=0;std::string text;AssetBytes bytes;std::vector<std::string> names;std::vector<N> values;N get(const std::string& name)const{auto it=std::find(names.begin(),names.end(),name);return it==names.end()?N{}:values.at(static_cast<std::size_t>(it-names.begin()));}};
N node(int kind){auto n=std::make_shared<Node>();n->kind=kind;return n;}
AssetBytes byte_array(std::span<const std::uint8_t> data){if(data.empty()){static const auto empty=std::make_shared<std::vector<std::uint8_t>>();return empty;}return std::make_shared<std::vector<std::uint8_t>>(data.begin(),data.end());}
struct Reader {
 struct Class {std::string name;std::vector<std::string> names;std::vector<std::uint8_t> types,extra;};
 std::span<const std::uint8_t> data;std::size_t pos=0;int nulls=0,root=0;std::map<int,N> objects;std::map<int,Class> classes;
 std::span<const std::uint8_t> take(std::size_t count){if(count>data.size()-pos)bad();auto s=data.subspan(pos,count);pos+=count;return s;}
 std::uint8_t byte(){return take(1)[0];}
 std::uint64_t number(std::size_t count){auto s=take(count);std::uint64_t v=0;for(std::size_t i=0;i<count;++i)v|=std::uint64_t(s[i])<<(i*8);return v;}
 std::int32_t integer(){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(number(4)));}
 int count(){auto n=integer();if(n<0||static_cast<std::size_t>(n)>data.size()-pos)bad();return n;}
 std::string string(){std::uint32_t len=0;for(int shift=0;;shift+=7){if(shift>28)bad();auto b=byte();len|=std::uint32_t(b&127)<<shift;if(!(b&128))break;}if(std::bit_cast<std::int32_t>(len)<0)bad();auto s=take(len);return utf8(utf16_replacing(std::string_view(reinterpret_cast<const char*>(s.data()),s.size())));}
 N resolve(N n)const{if(n&&n->kind==101){auto it=objects.find(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n->bits)));return it==objects.end()?N{}:it->second;}return n;}
 N field(N n,const std::string& name)const{return n?resolve(n->get(name)):N{};}
 std::uint8_t info(std::uint8_t type){switch(type){case 0:case 7:return byte();case 3:string();return 0;case 4:string();integer();return 0;case 1:case 2:case 5:case 6:return 0;default:bad();}}
 N primitive(std::uint8_t type){auto n=node(type);switch(type){case 1:n->bits=byte()!=0;break;case 2:case 10:n->bits=byte();break;case 3:{if(pos==data.size())bad();auto c=data[pos];auto s=take(c<128?1:c<224?2:c<240?3:4);auto chars=utf16_replacing(std::string_view(reinterpret_cast<const char*>(s.data()),s.size()));if(chars.empty())bad();n->bits=chars[0];break;}case 5:n->text=string();decimal(n->text);break;case 6:case 9:case 12:case 16:n->bits=number(8);break;case 7:case 14:n->bits=number(2);break;case 8:case 11:case 15:n->bits=number(4);break;case 13:n->bits=number(8);AssetDateTime::from_serialized(n->bits);break;case 17:return {};case 18:n->text=string();break;default:bad();}return n;}
 N array(int len,int depth,std::optional<std::uint8_t> primitive_type={}){if(primitive_type&&*primitive_type==2){auto n=node(102);n->bytes=byte_array(take(static_cast<std::size_t>(len)));return n;}auto n=node(103);n->values.resize(static_cast<std::size_t>(len));for(auto& value:n->values){if(nulls>0){--nulls;continue;}value=primitive_type?primitive(*primitive_type):record(byte(),depth);}nulls=0;return n;}
 N members(int id,const Class& c,int depth){auto n=node(100);n->text=c.name;n->names=c.names;n->values.resize(c.names.size());objects[id]=n;for(std::size_t i=0;i<c.names.size();++i){if(nulls>0){--nulls;continue;}n->values[i]=!c.types.empty()&&c.types[i]==0?primitive(c.extra[i]):record(byte(),depth);}nulls=0;return n;}
 N record(std::uint8_t type,int depth){if(++depth>64)bad();switch(type){
 case 1:{auto id=integer();auto ci=integer();auto it=classes.find(ci);if(it==classes.end())bad();auto c=it->second;return members(id,c,depth);}
 case 2:case 3:case 4:case 5:{auto id=integer();Class c;c.name=string();c.names.resize(static_cast<std::size_t>(count()));for(auto& name:c.names)name=string();if(type>=4){auto bytes=take(c.names.size());c.types.assign(bytes.begin(),bytes.end());for(auto t:c.types)c.extra.push_back(info(t));}if(type==3||type==5)integer();classes[id]=c;return members(id,c,depth);}
 case 6:{auto id=integer();auto n=node(18);n->text=string();objects[id]=n;return n;}
 case 7:{auto id=integer();if(byte()!=0||integer()!=1)bad();auto len=count();auto element=byte(),extra=info(element);auto n=array(len,depth,element==0?std::optional(extra):std::nullopt);objects[id]=n;return n;}
 case 8:return primitive(byte());case 9:{auto n=node(101);n->bits=static_cast<std::uint32_t>(integer());return n;}case 10:return {};
 case 12:integer();string();return record(byte(),depth);
 case 13:case 14:{auto n=type==13?int(byte()):integer();if(n<1)bad();nulls=n-1;return {};}
 case 15:{auto id=integer();auto len=count();auto n=array(len,depth,byte());objects[id]=n;return n;}
 case 16:case 17:{auto id=integer();auto n=array(count(),depth);objects[id]=n;return n;}
 default:bad();}}
 N read(){if(byte()!=0)bad();root=integer();integer();if(integer()!=1||integer()!=0)bad();for(;;){auto t=byte();if(t==11){auto it=objects.find(root);return it==objects.end()?N{}:it->second;}record(t,0);nulls=0;}}
 bool uuid(N value,Uuid& result,bool strict){auto u=resolve(value);if(!u||u->kind!=100||(strict&&u->text!="OpenMetaverse.UUID"))return false;auto g=field(u,"Guid");if(!g||g->kind!=100||(strict&&g->text!="System.Guid"))return false;std::array<std::uint8_t,16>b{};for(auto [name,kind,start,len]:std::vector<std::tuple<std::string,int,int,int>>{{"_a",8,0,4},{"_b",7,4,2},{"_c",7,6,2}}){auto n=g->get(name);if(!n||n->kind!=kind)return false;for(int i=0;i<len;++i)b[static_cast<std::size_t>(start+i)]=static_cast<std::uint8_t>(n->bits>>((len-i-1)*8));}for(int i=0;i<8;++i){auto n=g->get(std::string("_")+char('d'+i));if(!n||n->kind!=2)return false;b[static_cast<std::size_t>(8+i)]=static_cast<std::uint8_t>(n->bits);}result=Uuid::from_bytes(b);return true;}
};
std::optional<std::string> text(N n){return n&&n->kind==18?std::optional(n->text):std::nullopt;}
struct Writer {
 std::vector<std::uint8_t> out;int next=20;std::map<const void*,int> arrays;std::vector<std::pair<int,AssetBytes>> pending;
 void byte(std::uint8_t b){out.push_back(b);}void num(std::uint64_t n,int len=4){for(int i=0;i<len;++i)byte(static_cast<std::uint8_t>(n>>(8*i)));}
 void str(std::string_view s){auto normalized=utf8(utf16_replacing(s));auto len=normalized.size();if(len>INT32_MAX)bad();while(len>=128){byte(static_cast<std::uint8_t>(len|128));len>>=7;}byte(static_cast<std::uint8_t>(len));out.insert(out.end(),normalized.begin(),normalized.end());}
 void library(int id,const std::string& name){byte(12);num(id);str(name);}
 void string(const std::optional<std::string>& s){if(!s){byte(10);return;}byte(6);num(next++);str(*s);}
 void data(const AssetBytes& bytes){if(!bytes){byte(10);return;}auto found=arrays.find(bytes.get());int id;if(found!=arrays.end())id=found->second;else{id=next++;arrays[bytes.get()]=id;pending.emplace_back(id,bytes);}byte(9);num(id);}
 void flush_arrays(){for(const auto& [id,bytes]:pending){byte(15);num(id);if(bytes->size()>INT32_MAX)bad();num(bytes->size());byte(2);out.insert(out.end(),bytes->begin(),bytes->end());}}
 struct Member {std::string name;std::uint8_t type,primitive=0;std::string klass;int lib=0;};
 void klass(int id,const std::string& name,const std::vector<Member>& members,int library){byte(library?5:4);num(static_cast<std::uint32_t>(id));str(name);num(members.size());for(const auto& m:members)str(m.name);for(const auto& m:members)byte(m.type);for(const auto& m:members){if(m.type==0||m.type==7)byte(m.primitive);else if(m.type==3)str(m.klass);else if(m.type==4){str(m.klass);num(m.lib);}}if(library)num(library);}
};
}
std::vector<std::uint8_t> AssetNrbfCodec::encode(const AssetBase& asset){Writer w;w.byte(0);w.num(1);w.num(UINT32_MAX);w.num(1);w.num(0);w.library(1,"OpenSim.Framework, Version=0.9.3.1, Culture=neutral, PublicKeyToken=null");w.library(2,"OpenMetaverseTypes, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
 w.klass(1,"OpenSim.Framework.AssetBase",{{"m_data",7,2},{"m_metadata",4,0,"OpenSim.Framework.AssetMetadata",1},{"m_uploadAttempts",0,8}},1);w.data(asset.data);
 auto meta=asset.metadata();if(!meta)w.byte(10);else{
 w.klass(4,"OpenSim.Framework.AssetMetadata",{{"m_fullid",4,0,"OpenMetaverse.UUID",2},{"m_id",1},{"m_name",1},{"m_description",1},{"m_creation_date",0,13},{"m_type",0,10},{"m_content_type",1},{"m_sha1",7,2},{"m_local",0,1},{"m_temporary",0,1},{"m_creatorid",1},{"m_flags",4,0,"OpenSim.Framework.AssetFlags",1}},1);
 w.klass(-5,"OpenMetaverse.UUID",{{"Guid",3,0,"System.Guid"}},2);std::vector<Writer::Member> guid;for(int i=0;i<11;++i)guid.push_back({std::string("_")+char('a'+i),0,static_cast<std::uint8_t>(i==0?8:i<3?7:2)});w.klass(-6,"System.Guid",guid,0);auto b=meta->full_id_.bytes();for(auto [start,len]:std::vector<std::pair<int,int>>{{0,4},{4,2},{6,2}})for(int i=len-1;i>=0;--i)w.byte(b[static_cast<std::size_t>(start+i)]);for(int i=8;i<16;++i)w.byte(b[static_cast<std::size_t>(i)]);
 w.string(meta->id_);w.string(meta->name);w.string(meta->description);w.num(meta->creation_date.serialized_data(),8);w.byte(static_cast<std::uint8_t>(meta->type));w.string(meta->content_type_);w.data(meta->sha1);w.byte(meta->local);w.byte(meta->temporary);w.string(meta->creator_id);w.klass(-13,"OpenSim.Framework.AssetFlags",{{"value__",0,8}},1);w.num(static_cast<std::uint32_t>(meta->flags));
 }w.num(static_cast<std::uint32_t>(asset.upload_attempts));w.flush_arrays();w.byte(11);return std::move(w.out);
}
bool AssetNrbfCodec::decode(std::span<const std::uint8_t> data,std::shared_ptr<AssetBase>& asset,bool file){asset.reset();try{Reader p{data};auto root=p.read();if(!root||root->kind!=100||root->text!="OpenSim.Framework.AssetBase")return false;auto field=[&](N n,const char* name){if(file&&std::find(n->names.begin(),n->names.end(),name)==n->names.end())bad();return p.field(n,name);};auto bytes=[&](N n){if(file&&n&&n->kind!=102)bad();return n&&n->kind==102?n->bytes:AssetBytes{};};auto string=[&](N n){if(file&&n&&n->kind!=18)bad();return text(n);};auto a=std::make_shared<AssetBase>();a->data=bytes(field(root,"m_data"));auto meta=field(root,"m_metadata");if(!meta){if(!file)return false;a->metadata({});}else{if(meta->kind!=100||meta->text!="OpenSim.Framework.AssetMetadata")return false;auto m=a->metadata();Uuid id;bool valid=p.uuid(meta->get("m_fullid"),id,file);if(file&&!valid)bad();if(valid)m->full_id(id);auto raw_id=field(meta,"m_id");if(file)m->id_=string(raw_id);else if(auto s=text(raw_id))m->id(s);m->name=string(field(meta,"m_name"));m->description=string(field(meta,"m_description"));m->creator_id=string(field(meta,"m_creatorid"));auto date=meta->get("m_creation_date");if(date&&date->kind==13){auto parsed=AssetDateTime::from_serialized(date->bits);m->creation_date=file?parsed:AssetDateTime(parsed.ticks(),parsed.kind());}else if(file)bad();auto content=field(meta,"m_content_type");if(file)m->content_type_=string(content);else if(auto s=text(content))m->content_type(s);auto type=meta->get("m_type");if(type&&type->kind==10)m->type=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(type->bits));else if(file)bad();for(auto [name,value]:std::vector<std::pair<const char*,bool*>>{{"m_local",&m->local},{"m_temporary",&m->temporary}}){auto n=meta->get(name);if(n&&n->kind==1)*value=n->bits!=0;else if(file)bad();}auto flags=field(meta,"m_flags");if(flags&&flags->kind==100){if(file&&flags->text!="OpenSim.Framework.AssetFlags")bad();flags=flags->get("value__");}if(flags&&flags->kind==8)m->flags=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(flags->bits));else if(file)bad();if(file)m->sha1=bytes(field(meta,"m_sha1"));}
 if(file){auto attempts=field(root,"m_uploadAttempts");if(!attempts||attempts->kind!=8)bad();a->upload_attempts=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(attempts->bits));}asset=std::move(a);return true;}catch(...){return false;}}
bool AssetNrbfCodec::decode_file(std::span<const std::uint8_t> data,std::shared_ptr<AssetBase>& asset){return decode(data,asset,true);}
bool AssetNrbfCodec::decode_legacy(std::span<const std::uint8_t> data,std::shared_ptr<AssetBase>& asset){return decode(data,asset,false);}
}

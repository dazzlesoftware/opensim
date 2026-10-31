// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/avatar_appearance_constants.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::framework {
namespace {
template<class T>T& required(const std::shared_ptr<T>& p){if(!p)throw std::invalid_argument("Null appearance component");return *p;}
}
void AvatarAppearance::default_wearables(){wearables=std::make_shared<Wearables>(AvatarWearable::defaults());}
void AvatarAppearance::default_params(){
    visual_params=std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{
        33,61,85,23,58,127,63,85,63,42,0,85,63,36,85,95,153,63,34,0,63,109,88,132,63,136,81,85,103,136,127,0,150,150,150,127,0,0,0,0,0,127,0,0,255,127,114,127,99,63,127,140,127,127,0,0,0,191,0,104,0,0,0,0,0,0,0,0,0,145,216,133,0,127,0,127,170,0,0,127,127,109,85,127,127,63,85,42,150,150,150,150,150,150,150,25,150,150,150,0,127,0,0,144,85,127,132,127,85,0,127,127,127,127,127,127,59,127,85,127,127,106,47,79,127,127,204,2,141,66,0,0,127,127,0,0,0,0,127,0,159,0,0,178,127,36,85,131,127,127,127,153,95,0,140,75,27,127,127,0,150,150,198,0,0,63,30,127,165,209,198,127,127,153,204,51,51,255,255,255,204,0,255,150,150,150,150,150,150,150,150,150,150,0,150,150,150,150,150,0,127,127,150,150,150,150,150,150,150,150,0,0,150,51,132,150,150,150});
}
AvatarAppearance::AvatarAppearance(){default_wearables();reset_baked_textures();default_params();set_size({.45f,.6f,1.9f});}
AvatarAppearance::AvatarAppearance(std::shared_ptr<Wearables> w,std::shared_ptr<TextureEntry> t,Bytes v){
    if(w)wearables=std::move(w);else default_wearables();
    if(t)texture=std::move(t);else reset_baked_textures();
    if(v)visual_params=std::move(v);else default_params();
    set_size({.45f,.6f,1.9f});
}
AvatarAppearance::AvatarAppearance(const AvatarAppearance* a,bool copy_wearables,bool copy_baked){
    if(!a){default_wearables();reset_baked_textures();default_params();set_size({.45f,.6f,1.9f});return;}
    serial=a->serial;hover_z=a->hover_z;
    if(copy_wearables&&a->wearables){
        wearables=std::make_shared<Wearables>(a->wearables->size());
        for(std::size_t i=0;i<wearables->size();++i){
            auto& dest=(*wearables)[i];dest=std::make_shared<AvatarWearable>();
            auto& source=required((*a->wearables)[i]);
            for(std::size_t j=0;j<source.count();++j){auto item=source.at(static_cast<int>(j));dest->add(item.item,item.asset);}
        }
    }else clear_wearables();
    if(a->texture){
        texture=std::make_shared<TextureEntry>(a->texture->bakes_bytes());
        if(copy_baked&&a->cache_items)cache_items=std::make_shared<WearableCacheItems>(*a->cache_items);
    }
    if(a->visual_params)visual_params=std::make_shared<std::vector<std::uint8_t>>(*a->visual_params);
    set_size(a->avatar_size());
    for(auto& attachment:a->attachments())append_attachment(std::make_shared<AvatarAttachment>(attachment));
}
AvatarAppearance::AvatarAppearance(const network::Value* map){unpack(map);}
void AvatarAppearance::reset_baked_textures(){texture=std::make_shared<TextureEntry>(default_texture_id);}
void AvatarAppearance::reset_appearance(){serial=0;reset_baked_textures();hover_z=0;}
void AvatarAppearance::clear_wearables(){wearables=std::make_shared<Wearables>(15);for(auto& w:*wearables)w=std::make_shared<AvatarWearable>();}
void AvatarAppearance::get_assets_from(const AvatarAppearance& a){
    auto& dst=required(wearables);auto& src=required(a.wearables);
    for(std::size_t i=0;i<std::min(dst.size(),src.size());++i){
        auto& target=required(dst[i]);auto& origin=required(src[i]);
        const auto count=std::min(target.count(),origin.count());
        for(std::size_t j=0;j<count;++j){auto item=target.at(static_cast<int>(j));auto asset=origin.asset(item.item);if(!asset.is_zero())target.add(item.item,asset);}
    }
}
void AvatarAppearance::set_size(Vector3 size){
    auto clamp=[](float v){return v>32?32.f:(v<.1f?.1f:v);};
    size_={clamp(size.x),clamp(size.y),clamp(size.z)};box_=size_;box_.z+=.2f;
    if(box_.x<.2f)box_.x=.2f;if(box_.y<.3f)box_.y=.3f;if(box_.z<1.2f)box_.z=1.2f;
    avatar_height=size_.z;
}
void AvatarAppearance::set_wearable(int index,const std::shared_ptr<AvatarWearable>& wearable){
    auto& w=required(wearables);
    if(index<0)throw std::out_of_range("Negative wearable index");
    if(static_cast<std::size_t>(index)>=w.size()){
        auto old=w.size();auto grown=std::make_shared<Wearables>(w);grown->resize(static_cast<std::size_t>(index)+1);
        wearables=grown;for(auto i=old;i<grown->size();++i)(*grown)[i]=std::make_shared<AvatarWearable>();
    }
    auto& target=required(wearables->at(index));target.clear();
    auto& source=required(wearable);
    for(std::size_t i=0;i<source.count();++i){auto item=source.at(static_cast<int>(i));target.add(item.item,item.asset);}
}
bool AvatarAppearance::set_visual_params(const Bytes& params){
    if(!params)return false;
    auto& current=required(visual_params);
    if(current.size()!=params->size()){visual_params=std::make_shared<std::vector<std::uint8_t>>(*params);return true;}
    bool changed=false;for(std::size_t i=0;i<current.size();++i)if(current[i]!=(*params)[i]){current[i]=(*params)[i];changed=true;}return changed;
}
bool AvatarAppearance::set_texture_entries(const std::shared_ptr<TextureEntry>& entry){
    if(!entry)return false;
    bool changed=false;auto converted=std::make_shared<TextureEntry>(default_texture_id);
    for(unsigned i=0;i<45;++i){
        auto face=entry->get_face(i);auto current=required(texture).faces[i];
        if(face->texture_id()==default_texture_id){if(current&&current->texture_id()!=default_texture_id)changed=true;}
        else{converted->get_face(i)->texture_id(face->texture_id());if(!current||current->texture_id()!=face->texture_id())changed=true;}
    }
    if(changed)texture=converted;return changed;
}
std::optional<std::size_t> AvatarAppearance::slot(int point)const{for(std::size_t i=0;i<slots_.size();++i)if(slots_[i]&&slots_[i]->point==point)return i;return {};}
std::size_t AvatarAppearance::insert_slot(int point){
    if(auto i=slot(point))return *i;
    if(free_slots_.empty()){slots_.push_back(AttachmentSlot{point,{}});return slots_.size()-1;}
    auto i=free_slots_.back();free_slots_.pop_back();slots_[i]=AttachmentSlot{point,{}};return i;
}
void AvatarAppearance::erase_slot(std::size_t index){slots_[index].reset();free_slots_.push_back(index);}
std::vector<AvatarAttachment> AvatarAppearance::attachments()const{
    std::lock_guard lock(mutex_);std::vector<AvatarAttachment> result;
    for(auto& slot:slots_)if(slot)for(auto& a:slot->items)result.push_back(required(a));return result;
}
void AvatarAppearance::append_attachment(const std::shared_ptr<AvatarAttachment>& a){
    std::lock_guard lock(mutex_);auto& attachment=required(a);auto& items=slots_[insert_slot(attachment.point)]->items;
    for(auto& old:items)if(required(old).item==attachment.item)return;items.push_back(a);
}
void AvatarAppearance::replace_attachment(const std::shared_ptr<AvatarAttachment>& a){std::lock_guard lock(mutex_);slots_[insert_slot(required(a).point)]->items={a};}
std::shared_ptr<AvatarAttachment> AvatarAppearance::attachment_for_item(const Uuid& item)const{
    std::lock_guard lock(mutex_);for(auto& slot:slots_)if(slot)for(auto& a:slot->items)if(required(a).item==item)return a;return {};
}
int AvatarAppearance::attachpoint(const Uuid& item)const{
    std::lock_guard lock(mutex_);for(auto& slot:slots_)if(slot)for(auto& a:slot->items)if(required(a).item==item)return slot->point;return 0;
}
bool AvatarAppearance::remove_attachment(int point,const Uuid& item){
    std::lock_guard lock(mutex_);auto index=slot(point);if(!index)return false;
    auto& list=slots_[*index]->items;
    for(auto i=list.begin();i!=list.end();++i)if(required(*i).item==item){list.erase(i);if(list.empty())erase_slot(*index);return true;}return false;
}
bool AvatarAppearance::detach_attachment(const Uuid& item){
    std::lock_guard lock(mutex_);for(auto& s:slots_)if(s)for(auto& a:s->items)if(required(a).item==item)return remove_attachment(s->point,item);return false;
}
void AvatarAppearance::clear_attachments(){std::lock_guard lock(mutex_);slots_.clear();free_slots_.clear();}
bool AvatarAppearance::set_attachment(int point,const Uuid& item,const Uuid& asset){
    if(point==0)return false;std::lock_guard lock(mutex_);
    if(item.is_zero()){auto i=slot(point);if(!i)return false;erase_slot(*i);return true;}
    if(auto existing=attachment_for_item(item)){
        if(!existing->asset.is_zero()&&existing->point==(point&127))return false;
        detach_attachment(existing->item);
    }
    if(point&128)append_attachment(std::make_shared<AvatarAttachment>(point&127,item,asset));
    else replace_attachment(std::make_shared<AvatarAttachment>(point,item,asset));return true;
}
bool AvatarAppearance::can_teleport(float version)const{
    if(static_cast<double>(version)>=.8)return true;auto& w=required(wearables);
    if(w.size()<=16)return true;for(std::size_t i=16;i<w.size();++i)if(required(w[i]).count()>0)return false;
    for(unsigned index:{40u,41u,42u,43u,44u}){auto f=required(texture).faces[index];if(f&&!f->texture_id().is_zero()&&f->texture_id()!=default_texture_id)return false;}return true;
}
std::string AvatarAppearance::description()const{
#ifdef _WIN32
    constexpr auto newline="\r\n";
#else
    constexpr auto newline="\n";
#endif
    std::string text="Serial: "+std::to_string(serial)+newline;
    auto& t=required(texture);
    for(unsigned i=0;i<45;++i)if(t.faces[i])text+="Texture: "+std::to_string(i)+" --> "+t.faces[i]->texture_id().str()+newline;
    for(auto& wearable:required(wearables)){auto& w=required(wearable);for(std::size_t i=0;i<w.count();++i){auto item=w.at(static_cast<int>(i));text+="Wearable: item="+item.item.str()+", asset="+item.asset.str()+newline;}}
    text+="Visual Params: ";for(auto value:required(visual_params))text+=std::to_string(value)+",";return text+"\n";
}
}

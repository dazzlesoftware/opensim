// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/avatar_attachment.hpp"
#include "opensim/framework/avatar_wearable.hpp"
#include "opensim/framework/avatar_visual_parameter.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/framework/wearable_cache_item.hpp"
#include "opensim/framework/entity_transfer_context.hpp"
#include <mutex>
namespace opensim::framework {
class AvatarAppearance {
public:
    using VPElement=AvatarVisualParameter;
    using Wearables=std::vector<std::shared_ptr<AvatarWearable>>;
    using Bytes=std::shared_ptr<std::vector<std::uint8_t>>;
    inline static const Uuid default_texture_id{"c228d1cf-4b5d-4ba8-84f4-899a0796aa97"};
    AvatarAppearance();
    AvatarAppearance(std::shared_ptr<Wearables> wearables,std::shared_ptr<TextureEntry> texture,Bytes visual);
    AvatarAppearance(const AvatarAppearance& appearance):AvatarAppearance(&appearance){}
    explicit AvatarAppearance(const AvatarAppearance* appearance,bool copy_wearables=true,bool copy_baked=true);
    explicit AvatarAppearance(const network::Value* map);
    int serial=0;
    Bytes visual_params;
    std::shared_ptr<TextureEntry> texture;
    std::shared_ptr<Wearables> wearables;
    std::shared_ptr<WearableCacheItems> cache_items;
    float avatar_height=0,hover_z=0;
    Vector3 avatar_size()const{return size_;}
    Vector3 avatar_box_size()const{return box_;}
    float avatar_feet_offset()const{return feet_offset_+animation_offset_;}
    void set_size(Vector3 size);
    void set_height(){} // The original method's entire calculation is commented out.
    void reset_appearance();
    void reset_baked_textures();
    void clear_wearables();
    void get_assets_from(const AvatarAppearance& appearance);
    void set_wearable(int index,const std::shared_ptr<AvatarWearable>& wearable);
    bool set_texture_entries(const std::shared_ptr<TextureEntry>& entry);
    bool set_visual_params(const Bytes& params);
    void set_appearance(const std::shared_ptr<TextureEntry>& entry,const Bytes& params){set_texture_entries(entry);set_visual_params(params);}
    std::vector<AvatarAttachment> attachments()const;
    void append_attachment(const std::shared_ptr<AvatarAttachment>& attachment);
    void replace_attachment(const std::shared_ptr<AvatarAttachment>& attachment);
    bool set_attachment(int point,const Uuid& item,const Uuid& asset);
    std::shared_ptr<AvatarAttachment> attachment_for_item(const Uuid& item)const;
    int attachpoint(const Uuid& item)const;
    bool detach_attachment(const Uuid& item);
    bool remove_attachment(int point,const Uuid& item);
    void clear_attachments();
    bool can_teleport(float version)const;
    std::string description()const;
    network::Value pack(const EntityTransferContext* context=nullptr);
    network::Value pack_for_notecard(bool no_huds=true);
    void unpack(const network::Value* map);
private:
    struct AttachmentSlot {int point;std::vector<std::shared_ptr<AvatarAttachment>> items;};
    mutable std::recursive_mutex mutex_;
    std::vector<std::optional<AttachmentSlot>> slots_;
    std::vector<std::size_t> free_slots_;
    Vector3 size_{.45f,.6f,1.9f},box_{.45f,.6f,1.9f};
    float feet_offset_=0,animation_offset_=0;
    void default_wearables();void default_params();
    std::optional<std::size_t> slot(int point)const;
    std::size_t insert_slot(int point);
    void erase_slot(std::size_t index);
};
}

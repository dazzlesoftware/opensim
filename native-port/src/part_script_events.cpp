// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/part_script_events.hpp"
#include <stdexcept>
namespace opensim::scene {
PartScriptEvents::PartScriptEvents(framework::Uuid id,PartScriptEventsBackend backend):part_(id),backend_(std::move(backend)){if(!backend_.has_group||!backend_.has_root||!backend_.has_scene||!backend_.is_root||!backend_.allowed_drop||!backend_.remove_targets||!backend_.email||!backend_.aggregate_group||!backend_.physics_events||!backend_.full_update)throw std::invalid_argument("Incomplete part script event backend");}
void PartScriptEvents::set(framework::Uuid key,std::uint64_t mask){{std::lock_guard lock(mutex_);auto it=events_.find(key);if(it!=events_.end()&&it->second==mask)return;events_[key]=mask;}aggregate();}
void PartScriptEvents::remove(framework::Uuid key){std::lock_guard lock(mutex_);auto it=events_.find(key);if(it==events_.end())return;auto mask=it->second;
 if((mask&script_event::any_target)&&backend_.has_group())backend_.remove_targets(key);
 if(aggregated_&script_event::email){if(!backend_.has_group()||!backend_.has_scene())throw std::invalid_argument("Null parent scene for script mailbox removal");auto email=backend_.email();if(email)email->remove(part_);}
 events_.erase(key);aggregate();
}
void PartScriptEvents::aggregate(){if(!backend_.has_group()||!backend_.has_root())return;aggregated_.store(0,std::memory_order_relaxed);{
 std::lock_guard lock(mutex_);for(const auto& [key,mask]:events_)aggregated_.store(aggregated_.load(std::memory_order_relaxed)|mask,std::memory_order_relaxed);
 }std::uint32_t flags=0;if(aggregated_&script_event::any_touch)flags|=0x80u;if(aggregated_&script_event::money)flags|=0x200u;if(backend_.allowed_drop())flags|=0x10000u;flags_=flags;
 if(aggregated_&script_event::email){if(!backend_.has_group()||!backend_.has_scene())throw std::invalid_argument("Null parent scene for script mailbox registration");auto email=backend_.email();if(email)email->add(part_);}
 if(!backend_.has_group())throw std::invalid_argument("Null parent group after mailbox registration");if(backend_.is_root())backend_.aggregate_group();else{backend_.physics_events();backend_.full_update();}
}
std::uint64_t PartScriptEvents::aggregated()const{return aggregated_.load(std::memory_order_relaxed);}
std::uint32_t PartScriptEvents::local_flags()const{return flags_.load(std::memory_order_relaxed);}
std::optional<std::uint64_t> PartScriptEvents::events(framework::Uuid key)const{std::lock_guard lock(mutex_);auto it=events_.find(key);return it==events_.end()?std::nullopt:std::optional(it->second);}
void PartScriptEvents::initial_state(std::uint64_t mask,std::uint32_t flags){std::lock_guard lock(mutex_);aggregated_=mask;flags_=flags;}
}

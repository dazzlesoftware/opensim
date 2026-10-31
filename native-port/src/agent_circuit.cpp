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
#include "opensim/framework/agent_circuit.hpp"
#include <stdexcept>
namespace opensim::framework {
std::optional<std::string> AgentCircuitData::viewer()const{
    if(!viewer_||viewer_->find(' ')!=std::string::npos)return viewer_;
    return channel.value_or("")+" "+*viewer_;
}
std::string AgentCircuitData::name()const{return first_name.value_or("")+" "+last_name.value_or("");}
AuthenticateResponse AgentCircuitManager::authenticate_session(const Uuid& session,const Uuid& agent,std::uint32_t code)const{
    std::lock_guard lock(mutex_);
    const auto found=circuits_.find(code);
    if(found==circuits_.end()||!found->second)return {};
    const auto& data=*found->second;
    if(data.session_id!=session||data.agent_id!=agent)return {};
    Login login;
    login.agent=agent;login.session=session;login.secure_session=data.secure_session_id;
    login.first=data.first_name;login.last=data.last_name;login.inventory_folder=data.inventory_folder;
    login.base_folder=data.base_folder;login.start_position=data.start_position;login.start_far=data.start_far;
    // CapsPath, CircuitCode and Appearance deliberately retain Login defaults:
    // the original AuthenticateSession does not populate them.
    return {true,std::move(login)};
}
void AgentCircuitManager::add_locked(const Circuit& data){
    const auto found=circuits_.find(data->circuit_code);
    if(found!=circuits_.end()&&found->second&&found->second->agent_id!=data->agent_id)by_agent_.erase(found->second->agent_id);
    circuits_[data->circuit_code]=data;by_agent_[data->agent_id]=data;
}
void AgentCircuitManager::add_new_circuit(const Circuit& data){
    if(!data)throw std::invalid_argument("Null agent circuit");
    data->child=true;
    std::lock_guard lock(mutex_);add_locked(data);
}
void AgentCircuitManager::add_new_circuit(std::uint32_t code,const Circuit& data){
    if(!data)throw std::invalid_argument("Null agent circuit");
    data->circuit_code=code;
    std::lock_guard lock(mutex_);add_locked(data);
}
void AgentCircuitManager::remove_circuit(std::uint32_t code){
    std::lock_guard lock(mutex_);
    auto found=circuits_.find(code);
    if(found!=circuits_.end()){by_agent_.erase(found->second->agent_id);circuits_.erase(found);}
}
void AgentCircuitManager::remove_circuit(const Uuid& agent){
    std::lock_guard lock(mutex_);
    auto found=by_agent_.find(agent);
    if(found!=by_agent_.end()){circuits_.erase(found->second->circuit_code);by_agent_.erase(found);}
}
void AgentCircuitManager::remove_circuit(const Circuit& data){
    if(!data)throw std::invalid_argument("Null agent circuit");
    std::lock_guard lock(mutex_);
    auto found=by_agent_.find(data->agent_id);
    if(found!=by_agent_.end()){
        if(found->second->circuit_code!=data->circuit_code)circuits_.erase(found->second->circuit_code);
        by_agent_.erase(found);
    }
    circuits_.erase(data->circuit_code);
}
AgentCircuitManager::Circuit AgentCircuitManager::get_agent_circuit_data(std::uint32_t code)const{
    std::lock_guard lock(mutex_);auto found=circuits_.find(code);return found==circuits_.end()?nullptr:found->second;
}
AgentCircuitManager::Circuit AgentCircuitManager::get_agent_circuit_data(const Uuid& agent)const{
    std::lock_guard lock(mutex_);auto found=by_agent_.find(agent);return found==by_agent_.end()?nullptr:found->second;
}
AgentCircuitManager::AgentIndex AgentCircuitManager::get_agent_circuits()const{std::lock_guard lock(mutex_);return by_agent_;}
void AgentCircuitManager::update_agent_data(const AgentCircuitData& data){
    std::lock_guard lock(mutex_);auto found=circuits_.find(data.circuit_code);
    if(found==circuits_.end())return;
    auto& original=*found->second;
    original.first_name=data.first_name;original.last_name=data.last_name;original.start_position=data.start_position;
    original.start_far=data.start_far;original.secure_session_id=data.secure_session_id;original.session_id=data.session_id;
}
bool AgentCircuitManager::try_change_circuit_code(std::uint32_t code,std::uint32_t new_code){
    std::lock_guard lock(mutex_);
    if(circuits_.contains(new_code))return false;
    auto found=circuits_.find(code);if(found==circuits_.end())return false;
    auto data=found->second;circuits_.erase(found);data->circuit_code=new_code;
    circuits_[new_code]=data;by_agent_[data->agent_id]=data;return true;
}
void AgentCircuitManager::update_agent_child_status(std::uint32_t code,bool child){std::lock_guard lock(mutex_);auto found=circuits_.find(code);if(found!=circuits_.end())found->second->child=child;}
bool AgentCircuitManager::get_agent_child_status(std::uint32_t code)const{std::lock_guard lock(mutex_);auto found=circuits_.find(code);return found!=circuits_.end()&&found->second->child;}
}

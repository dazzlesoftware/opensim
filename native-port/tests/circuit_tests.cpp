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
#include "opensim/network/lludp.hpp"
#include <iostream>
#include <thread>
#include <vector>
using namespace opensim::framework;
namespace {
int failures=0;
void check(bool okay,const char* name){if(!okay){std::cerr<<"FAIL: "<<name<<'\n';++failures;}}
auto circuit(std::uint32_t code,const Uuid& agent,const Uuid& session){
    auto data=std::make_shared<AgentCircuitData>();data->circuit_code=code;data->agent_id=agent;data->session_id=session;
    data->secure_session_id=Uuid::random();data->first_name="First";data->last_name="Last";
    data->base_folder=Uuid::random();data->inventory_folder=Uuid::random();data->start_position={5,23,125};data->start_far=512;
    return data;
}
void manager_tests(){
    const Uuid agent=Uuid::random(),other=Uuid::random(),session=Uuid::random();
    AgentCircuitManager manager;auto first=circuit(1,agent,session);
    manager.add_new_circuit(1,first);
    check(!first->child,"explicit circuit-code overload preserves child status");
    auto response=manager.authenticate_session(session,agent,1);
    check(response.authorised&&response.login_info.has_value(),"triple circuit authentication");
    const auto& login=*response.login_info;
    check(login.agent==agent&&login.session==session&&login.secure_session==first->secure_session_id,"authentication session data");
    check(login.first==first->first_name&&login.base_folder==first->base_folder&&login.inventory_folder==first->inventory_folder&&login.start_position==first->start_position&&login.start_far==512,"authentication login payload");
    check(login.circuit_code==0&&login.caps_path.empty()&&!login.appearance,"authentication preserves source Login defaults");
    check(!manager.authenticate_session(Uuid::random(),agent,1).authorised&&!manager.authenticate_session(session,other,1).authorised&&!manager.authenticate_session(session,agent,2).authorised,"wrong session/agent/code rejected");
    auto snapshot=manager.get_agent_circuits();manager.remove_circuit(1);
    check(snapshot.at(agent)==first&&manager.get_agent_circuits().empty(),"snapshot copies index but retains circuit references");
    manager.add_new_circuit(first);check(first->child,"implicit circuit-code overload marks child");
    auto replacement=circuit(1,other,session);manager.add_new_circuit(replacement);
    check(!manager.get_agent_circuit_data(agent)&&manager.get_agent_circuit_data(1)==replacement,"code collision removes previous agent index");
    auto another=circuit(2,other,Uuid::random());manager.add_new_circuit(another);
    check(manager.get_agent_circuit_data(1)==replacement&&manager.get_agent_circuit_data(other)==another,"same agent may retain multiple circuit-code entries");
    manager.remove_circuit(replacement);
    check(!manager.get_agent_circuit_data(1)&&!manager.get_agent_circuit_data(2)&&!manager.get_agent_circuit_data(other),"object removal cleans both old and current code entries");
    manager.add_new_circuit(10,first);
    check(manager.try_change_circuit_code(10,11)&&first->circuit_code==11&&!manager.get_agent_circuit_data(10),"circuit code change updates aliased data");
    check(!manager.try_change_circuit_code(11,11)&&!manager.try_change_circuit_code(99,12),"occupied or missing circuit-code change rejected");
    AgentCircuitData update;update.circuit_code=11;update.first_name="Updated";update.session_id=Uuid::random();update.secure_session_id=Uuid::random();update.start_position={10,20,30};update.start_far=64;
    manager.update_agent_data(update);
    check(first->first_name==update.first_name&&first->session_id==update.session_id&&first->agent_id==agent,"agent update changes only source-selected fields");
    manager.update_agent_child_status(11,false);check(!manager.get_agent_child_status(11)&&!manager.get_agent_child_status(99),"child status lookup/update");
    manager.remove_circuit(agent);check(!manager.get_agent_circuit_data(11),"remove by agent");
    first->channel="Viewer";first->viewer("1.2.3");check(first->viewer()=="Viewer 1.2.3","modern viewer version normalization");
    first->viewer("OtherViewer 1.2.3");check(first->viewer()=="OtherViewer 1.2.3","old viewer version normalization");
    first->viewer(std::nullopt);check(!first->viewer(),"nullable viewer normalization");
}
void concurrent_tests(){
    AgentCircuitManager manager;
    std::vector<std::thread> workers;
    for(std::uint32_t worker=0;worker<8;++worker)workers.emplace_back([&manager,worker]{
        for(std::uint32_t i=0;i<100;++i){
            const auto code=worker*1000+i;const auto agent=Uuid::random(),session=Uuid::random();
            auto data=circuit(code,agent,session);manager.add_new_circuit(code,data);
            if(!manager.authenticate_session(session,agent,code).authorised)std::terminate();
            manager.remove_circuit(code);
        }
    });
    for(auto& worker:workers)worker.join();
    check(manager.get_agent_circuits().empty(),"concurrent admission/removal keeps indexes consistent");
}
void codec_tests(){
    using namespace opensim::network::lludp;
    check(packet_catalog().size()==387,"all baseline packet types present");
    auto packet=make_packet("UseCircuitCode");packet.sequence=42;
    const Uuid agent=Uuid::random(),session=Uuid::random();
    packet.block("CircuitCode")["Code"]=std::uint64_t{123456};packet.block("CircuitCode")["ID"]=agent;packet.block("CircuitCode")["SessionID"]=session;
    const auto wire=encode_datagram(packet);auto parsed=decode_datagram(wire);
    check(parsed.sequence==42&&std::get<Uuid>(parsed.block("CircuitCode").at("ID"))==agent&&std::get<Uuid>(parsed.block("CircuitCode").at("SessionID"))==session,"UseCircuitCode wire credentials");
    for(std::size_t length=0;length<wire.size();++length){
        bool rejected=false;try{decode_datagram(std::span(wire).first(length));}catch(const std::exception&){rejected=true;}
        check(rejected,"truncated circuit packet rejected");
    }
    std::vector<std::uint8_t> zeros(6+2048);zeros[0]=0x80;zeros[1]=1;
    check(zero_decode(zero_encode(zeros))==zeros,"long zero runs round trip");
    bool rejected=false;try{zero_decode(zero_encode(zeros),100);}catch(const std::length_error&){rejected=true;}
    check(rejected,"zero expansion limit");
}
}
int main(){try{manager_tests();concurrent_tests();codec_tests();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}if(failures)return 1;std::cout<<"Circuit and LLUDP validation passed\n";}

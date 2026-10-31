#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/network/circuit_transport.hpp"
#include "opensim/network/rpc.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    framework::AgentCircuitData source;
    source.agent_id=framework::Uuid::random();source.session_id=framework::Uuid::random();source.secure_session_id=framework::Uuid::random();source.circuit_code=42;
    source.appearance=std::make_shared<framework::AvatarAppearance>();source.appearance->serial=17;
    source.appearance->texture->get_face(44)->texture_id(framework::Uuid("00000000-0000-0000-0000-000000000123"));
    source.service_urls=std::make_shared<framework::AgentCircuitData::ServiceUrls>();(*source.service_urls)["z"]=std::string("z");(*source.service_urls)["a"]=std::string("a");
    framework::EntityTransferContext context;auto wire=network::llsd_xml(source.pack(&context));
    auto decoded=std::make_shared<framework::AgentCircuitData>();decoded->unpack(network::parse_llsd_xml(wire));
    check(decoded->appearance&&decoded->appearance!=source.appearance,"Appearance decoded into its own model");
    check(decoded->appearance->texture->get_face(44)->texture_id()==source.appearance->texture->get_face(44)->texture_id(),"Extended bake survived circuit XML");
    auto keys=network::parse_llsd_xml(wire).get("serviceurls").ordered_keys();check(keys==std::vector<std::string>({"z","a"}),"Modern URL map retained dictionary order");
    auto alias=decoded->service_urls;alias->erase("z");(*alias)["new"]=std::string("new");check(decoded->service_urls->begin()->first=="new","Dictionary aliases and removed-slot reuse");
    framework::AgentCircuitManager manager;manager.add_new_circuit(42,decoded);bool admitted=false;
    network::CircuitTransport transport(manager,[](const auto&,auto){return true;},[&](const auto& circuit,const auto& login,const auto&){
        check(circuit==decoded&&circuit->appearance->serial==17,"Transport admission received decoded appearance");
        check(!login.appearance,"Legacy authentication result does not fabricate appearance");admitted=true;return true;
    });
    auto use=network::lludp::make_packet("UseCircuitCode");use.sequence=123;
    use.block("CircuitCode")["Code"]=std::uint64_t{42};use.block("CircuitCode")["ID"]=source.agent_id;use.block("CircuitCode")["SessionID"]=source.session_id;
    transport.receive({"127.0.0.1",10000},network::lludp::encode_datagram(use));
    check(admitted&&transport.connection(source.agent_id).has_value(),"Circuit XML to credential-gated transport handoff");
    // Ordered key enumeration includes untracked direct map additions and does
    // not emit duplicates when a recorded key is overwritten.
    auto map=network::Value::object({});map.set("z",network::Value(1));map.set("a",network::Value(2));map.set("z",network::Value(3));map.map["last"]=network::Value(4);
    check(map.ordered_keys()==std::vector<std::string>({"z","a","last"}),"Mixed map insertion paths");
    auto call=network::RpcCall{"order",{map}};auto rpc=network::parse_xml_rpc(network::xml_rpc_request(call));
    check(rpc.parameters[0].ordered_keys()==map.ordered_keys(),"XML-RPC struct insertion order");
    std::cout<<"PASS: appearance circuit XML, ordered maps and transport handoff\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_running.hpp"
namespace opensim::scene {
GetScriptRunningController::GetScriptRunningController(ScriptRunningBackend backend):backend_(std::move(backend)){if(!backend_.find_instance||!backend_.reply)throw std::invalid_argument("Null script-running backend");}
bool GetScriptRunningController::request(framework::Uuid agent,framework::Uuid object,framework::Uuid item)const{
 auto instance=backend_.find_instance(item);if(!instance)return false;if(backend_.trace)backend_.trace(object,item);bool running=instance->running();instance->set_item_running(running);backend_.reply(agent,object,item,running);return true;
}
network::lludp::Packet script_running_reply_packet(framework::Uuid object,framework::Uuid item,bool running){auto packet=network::lludp::make_packet("ScriptRunningReply");auto& block=packet.block("Script");block["ObjectID"]=object;block["ItemID"]=item;block["Running"]=running;return packet;}
}

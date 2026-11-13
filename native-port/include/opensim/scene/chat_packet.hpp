// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/lludp.hpp"
#include "opensim/framework/vector_text.hpp"
namespace opensim::scene {
struct ChatMessage {
 std::string message,from_name;
 framework::Uuid source,owner;
 framework::Vector3 position;
 std::uint8_t type = 1, source_type = 2, audible = 1;
};
network::lludp::Packet chat_packet(const ChatMessage& message);
}

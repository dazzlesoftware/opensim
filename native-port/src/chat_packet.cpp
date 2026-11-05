// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/chat_packet.hpp"
namespace opensim::scene {
namespace {
std::vector<std::uint8_t> terminated(std::string_view text, std::size_t limit) {
    auto nul = text.find('\0');
    if (nul != std::string_view::npos)
        text = text.substr(0, nul);
    if (text.empty())
        return {};
    auto size = std::min(text.size(), limit - 1);
    if (size < text.size())
        while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xc0) == 0x80)
            --size;
    if (size == 0)
        return {};
    std::vector<std::uint8_t> bytes(text.begin(), text.begin() + size);
    bytes.push_back(0);
    return bytes;
}
}
network::lludp::Packet chat_packet(const ChatMessage& message) {
    auto p = network::lludp::make_packet("ChatFromSimulator");
    auto& b = p.block("ChatData");
    b["FromName"] = terminated(message.from_name, 255);
    b["Message"] = terminated(message.message, 1024);
    b["SourceID"] = message.source;
    b["OwnerID"] = message.owner;
    b["SourceType"] = std::uint64_t(message.source_type);
    b["ChatType"] = std::uint64_t(message.type);
    b["Audible"] = std::uint64_t(message.audible);
    b["Position"] = std::vector<double>{message.position.x, message.position.y, message.position.z};
    return p;
}
}

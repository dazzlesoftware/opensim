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
#include "opensim/network/unacked_packets.hpp"
#include <bit>
#include <chrono>
#include <stdexcept>
namespace opensim::network {
namespace {
std::int32_t tick(){const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return static_cast<std::int32_t>(static_cast<std::uint64_t>(now)&0x7fffffffu);}
std::int32_t subtract(std::int32_t a,std::int32_t b){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a)-static_cast<std::uint32_t>(b));}
}
UnackedPacketCollection::UnackedPacketCollection(Clock clock,FreeBuffer free_buffer):clock_(clock?std::move(clock):Clock{tick}),free_buffer_(std::move(free_buffer)){}
void UnackedPacketCollection::require_open()const{if(closed_)throw std::logic_error("Unacked packet collection was cleared");}
void UnackedPacketCollection::clear(){
    std::lock_guard consumer(consumer_mutex_);std::lock_guard pending(pending_mutex_);
    packets_.clear();pending_adds_.clear();pending_acks_.clear();pending_removes_.clear();oldest_=0;closed_=true;
}
std::int32_t UnackedPacketCollection::count()const{
    std::lock_guard consumer(consumer_mutex_);std::lock_guard pending(pending_mutex_);require_open();
    const auto size=static_cast<std::uint32_t>(packets_.size()+pending_adds_.size()-pending_acks_.size()-pending_removes_.size());
    return std::bit_cast<std::int32_t>(size);
}
void UnackedPacketCollection::add(const Packet& packet){
    std::lock_guard lock(pending_mutex_);require_open();pending_adds_.push_back(packet);
    if(!packet||!packet->client||!packet->buffer)throw std::invalid_argument("Invalid outgoing reliable packet");
    packet->client->unacked_bytes.fetch_add(packet->buffer->data_length);
}
void UnackedPacketCollection::acknowledge(std::uint32_t sequence,std::int32_t time,bool resend){std::lock_guard lock(pending_mutex_);require_open();pending_acks_.push_back({sequence,time,resend});}
void UnackedPacketCollection::remove(std::uint32_t sequence){std::lock_guard lock(pending_mutex_);require_open();pending_removes_.push_back(sequence);}
void UnackedPacketCollection::free(const Packet& packet){
    if(packet&&packet->buffer){
        packet->client->unacked_bytes.fetch_sub(packet->buffer->data_length);
        if(free_buffer_)free_buffer_(packet->buffer);
        packet->buffer.reset();
    }
}
void UnackedPacketCollection::process_queues(){
    for(;;){
        std::uint32_t sequence;
        {std::lock_guard lock(pending_mutex_);require_open();if(pending_removes_.empty())break;sequence=pending_removes_.front();pending_removes_.pop_front();}
        auto found=packets_.find(sequence);if(found!=packets_.end()){auto packet=found->second;packets_.erase(found);free(packet);}
    }
    for(;;){
        Packet packet;
        {std::lock_guard lock(pending_mutex_);if(pending_adds_.empty())break;packet=pending_adds_.front();pending_adds_.pop_front();}
        if(packet)packets_[packet->sequence_number]=packet;
    }
    for(;;){
        PendingAck ack;
        {std::lock_guard lock(pending_mutex_);if(pending_acks_.empty())break;ack=pending_acks_.front();pending_acks_.pop_front();}
        auto found=packets_.find(ack.sequence);
        if(found!=packets_.end()){
            auto packet=found->second;packets_.erase(found);
            if(packet){free(packet);if(packet->client->acknowledge_packets)packet->client->acknowledge_packets(1);}
        }
    }
}
std::optional<std::vector<UnackedPacketCollection::Packet>> UnackedPacketCollection::get_expired_packets(std::int32_t timeout){
    std::lock_guard consumer(consumer_mutex_);process_queues();std::optional<std::vector<Packet>> expired;
    bool first=true;
    if(!packets_.empty()){
        const auto now=clock_()&0x7fffffff;
        for(const auto& [sequence,packet]:packets_){
            if(!packet->buffer){remove(sequence);continue;}
            if(first){oldest_=packet->sequence_number;first=false;}
            const auto sent=packet->tick_count.exchange(0);
            if(sent==0)continue;
            if(subtract(now,sent)<timeout){const auto previous=packet->tick_count.exchange(sent);if(previous>sent)packet->tick_count.exchange(previous);continue;}
            if(!expired)expired.emplace();
            if(packet->client->expire_packets)packet->client->expire_packets(1);
            expired->push_back(packet);
        }
    }
    return expired;
}
}

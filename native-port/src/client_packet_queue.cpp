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
#include "opensim/network/client_packet_queue.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
namespace opensim::network {
namespace {
float read_float(std::span<const std::uint8_t> bytes,std::size_t offset){
    std::uint32_t bits=0;for(int i=0;i<4;++i)bits|=static_cast<std::uint32_t>(bytes[offset+i])<<(8*i);return std::bit_cast<float>(bits);
}
void write_float(std::vector<std::uint8_t>& bytes,std::size_t offset,float value){const auto bits=std::bit_cast<std::uint32_t>(value);for(int i=0;i<4;++i)bytes[offset+i]=static_cast<std::uint8_t>(bits>>(8*i));}
}
ClientPacketQueue::ClientPacketQueue(const ThrottleRates& rates,TokenBucket* parent,TokenBucket::Clock clock)
    :max_rate_(static_cast<float>(rates.client_max_rate)),burst_time_(rates.burst_time),flow_(parent,max_rate_,max_rate_*burst_time_,rates.adaptive_throttles_enabled,clock){
    for(int i=0;i<7;++i){const auto rate=static_cast<float>(rates.get_rate(static_cast<ThrottleOutPacketType>(i)));buckets_[i]=std::make_unique<TokenBucket>(&flow_,rate,rate*burst_time_,clock);}
}
bool ClientPacketQueue::enqueue(const Packet& packet){
    if(closed_)return false;
    if(!packet)throw std::invalid_argument("Missing outgoing packet");
    const auto category=static_cast<int>(packet->category);if(category<0||category>=7)return false;
    queues_[category].push_back(packet);return true;
}
bool ClientPacketQueue::drain(const Send& send,const std::function<int()>& unacked_count,const Empty& empty){
    if(closed_)return false;
    bool sent=false;int empty_flags=0;
    for(int i=0;i<7&&!closed_;++i){
        // Source always services one resend before applying the ACK stall gate.
        if(i==1&&unacked_count()>50)return true;
        auto packet=next_[i];const bool held=static_cast<bool>(packet);
        if(!packet&&!queues_[i].empty()){packet=queues_[i].front();queues_[i].pop_front();}
        if(!packet){if(i>0)empty_flags|=1<<(i-1);continue;}
        if(!packet->buffer){next_[i].reset();if(i>0&&queues_[i].size()<5)empty_flags|=1<<(i-1);continue;}
        if(buckets_[i]->remove_tokens(packet->buffer->data_length)){
            send(packet);sent=true;if(held)next_[i].reset();
            if(i>0&&queues_[i].size()<5)empty_flags|=1<<(i-1);
        }else next_[i]=packet;
    }
    if(!closed_&&empty_flags&&empty)empty(static_cast<ThrottleOutPacketTypeFlags>(empty_flags));
    return sent;
}
void ClientPacketQueue::clear(){closed_=true;for(auto& queue:queues_)queue.clear();for(auto& packet:next_)packet.reset();}
std::size_t ClientPacketQueue::queued(ThrottleOutPacketType category)const{const auto index=static_cast<int>(category);return index>=0&&index<7?queues_[index].size():0;}
std::size_t ClientPacketQueue::queued()const{std::size_t count=0;for(const auto& queue:queues_)count+=queue.size();return count;}
void ClientPacketQueue::set_throttles(std::span<const std::uint8_t> data,float factor){
    if(data.size()<28)throw std::out_of_range("Viewer throttles require seven little-endian floats");
    float scale=.125f*factor,total=0;std::array<float,7> rates;
    for(int i=0;i<7;++i){rates[i]=std::clamp(read_float(data,i*4)*scale,(i==2||i==3)?50.f:1000.f,max_rate_);total+=rates[i];}
    if(total>max_rate_){scale=max_rate_/total;for(auto& rate:rates)rate*=scale;}
    // Preserve the source assignment order, including the asset category first.
    for(int i:{0,1,2,3,6,4,5}){buckets_[i]->requested_drip_rate(rates[i]);buckets_[i]->requested_burst(rates[i]*burst_time_);}
}
std::vector<std::uint8_t> ClientPacketQueue::packed_throttles(float multiplier)const{
    std::vector<std::uint8_t> bytes(28);multiplier*=8;
    for(int i=0;i<7;++i)write_float(bytes,i*4,buckets_[i]->requested_drip_rate()*multiplier);return bytes;
}
int ClientPacketQueue::bytes_can_send(ThrottleOutPacketType category,int time_ms)const{const auto index=static_cast<int>(category);return index>0&&index<7?buckets_[index]->get_cat_bytes_can_send(time_ms):0;}
void ClientPacketQueue::force_setting(int category,int setting,int mtu){if(category>0&&category<7)buckets_[category]->requested_drip_rate(static_cast<float>(std::max(setting,mtu)));}
int ClientPacketQueue::setting(int category)const{
    if(category<=0||category>=7)return 0;
    const auto value=buckets_[category]->requested_drip_rate();
    return value>=-2147483648.f&&value<2147483648.f?static_cast<int>(value):(-2147483647-1);
}
}

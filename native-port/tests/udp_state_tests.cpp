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
#include "opensim/network/packet_history.hpp"
#include "opensim/network/unacked_packets.hpp"
#include "opensim/network/udp_socket.hpp"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <thread>
using namespace opensim::network;
namespace {
int failures=0;
void check(bool okay,const char* name){if(!okay){std::cerr<<"FAIL: "<<name<<'\n';++failures;}}
template<class F>bool throws(F&& call){try{call();}catch(const std::exception&){return true;}return false;}
void history_tests(){
    IncomingPacketHistoryCollection history(4);
    check(history.try_enqueue(1)&&history.try_enqueue(2)&&history.try_enqueue(3),"history accepts initial packets");
    check(!history.try_enqueue(1),"history suppresses duplicates");
    check(history.try_enqueue(4)&&history.try_enqueue(1),"history evicts after capacity minus one packets");
    IncomingPacketHistoryCollection one(1);check(one.try_enqueue(0)&&one.try_enqueue(0),"capacity one retains source empty-history behavior");
    IncomingPacketHistoryCollection empty(0);check(throws([&]{empty.try_enqueue(7);})&&!empty.try_enqueue(7),"zero-capacity failure retains source hash mutation");
    IncomingPacketHistoryCollection wrap(4);check(wrap.try_enqueue(0xffffffffu)&&wrap.try_enqueue(0)&&!wrap.try_enqueue(0xffffffffu),"sequence wrap and duplicate identity");
}
void unacked_tests(){
    auto client=std::make_shared<UdpClientAccounting>();int acks=0,expired=0,freed=0,now=1000;
    client->acknowledge_packets=[&](int n){acks+=n;};client->expire_packets=[&](int n){expired+=n;};
    UnackedPacketCollection packets([&]{return now;},[&](const auto&){++freed;});
    auto make=[&](std::uint32_t sequence,int length,int sent){auto p=std::make_shared<OutgoingPacket>();p->client=client;p->sequence_number=sequence;p->buffer=std::make_shared<PacketBuffer>();p->buffer->data_length=length;p->tick_count=sent;return p;};
    auto first=make(10,100,900);packets.add(first);
    check(packets.count()==1&&client->unacked_bytes==100,"pending reliable send accounting");
    check(!packets.get_expired_packets(101)&&first->tick_count==900&&packets.oldest()==10,"unexpired packet restores send time");
    auto list=packets.get_expired_packets(100);
    check(list&&list->size()==1&&list->front()==first&&first->tick_count==0&&expired==1,"expiry queues resend and applies flow feedback");
    check(!packets.get_expired_packets(1),"resend not sent over wire cannot expire twice");
    first->tick_count=1000;now=1100;
    check(packets.get_expired_packets(100).has_value()&&expired==2,"restamped resend can expire again");
    packets.acknowledge(10,1100,true);
    check(packets.count()==0&&client->unacked_bytes==100,"ACK effects deferred until queue processing");
    check(!packets.get_expired_packets(0)&&!first->buffer&&client->unacked_bytes==0&&acks==1&&freed==1,"ACK releases buffer and updates flow accounting");
    check(packets.oldest()==10,"oldest remains stale when collection becomes empty");
    packets.acknowledge(999,1100,false);check(packets.count()==-1,"estimated pending count can be negative");packets.get_expired_packets(0);
    auto second=make(20,50,1000);packets.remove(20);packets.add(second);packets.get_expired_packets(1000);
    check(packets.count()==1&&second->buffer!=nullptr,"pending removes are processed before pending adds");
    packets.remove(20);packets.get_expired_packets(1000);
    check(!second->buffer&&acks==1&&client->unacked_bytes==0,"explicit removal releases without ACK feedback");
    auto high=make(0xffffffffu,1,1000),low=make(0,1,1000);packets.add(high);packets.add(low);packets.get_expired_packets(1000);
    check(packets.oldest()==0,"oldest follows unsigned sorted key ordering after wrap");
    packets.clear();check(packets.oldest()==0&&throws([&]{packets.count();}),"clear closes queue objects like source teardown");
}
void socket_tests(const std::string& address){
    std::mutex mutex;std::condition_variable changed;std::vector<std::vector<std::uint8_t>> replies;
    UdpSocket* receiver=nullptr;
    UdpSocket server({address,0,0,128},[&](const UdpEndpoint& source,std::span<const std::uint8_t> bytes){receiver->send_to(source,bytes);});receiver=&server;
    const auto bound=server.start();
    UdpSocket client({address,0,0,65507},[&](const UdpEndpoint&,std::span<const std::uint8_t> bytes){std::lock_guard lock(mutex);replies.emplace_back(bytes.begin(),bytes.end());changed.notify_all();});client.start();
    const std::vector<std::uint8_t> first{1,2,3,4};check(client.send_to(bound,first),"UDP datagram send");
    {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(2),[&]{return !replies.empty();}),"UDP datagram receives echo");check(!replies.empty()&&replies.front()==first,"UDP datagram boundaries and data");}
    UdpSocket duplicate({address,bound.port,0,128},[](const auto&,auto){});check(throws([&]{duplicate.start();}),"duplicate UDP bind rejected");
    client.send_to(bound,std::vector<std::uint8_t>(129));client.send_to(bound,std::vector<std::uint8_t>{5});
    {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(2),[&]{return replies.size()>=2;}),"UDP continues after oversized datagram");}
    check(server.statistics().oversized==1&&server.statistics().received==3,"UDP oversized receive accounting");
    check(!server.send_to(bound,std::vector<std::uint8_t>(129)),"UDP outgoing datagram limit");
    const auto before=std::chrono::steady_clock::now();client.stop();server.stop();
    check(std::chrono::steady_clock::now()-before<std::chrono::seconds(1)&&!server.is_running(),"UDP stop joins idle receive loop promptly");
    check(!server.send_to(bound,first),"stopped UDP socket rejects sends");
    UdpSocket rebound({address,bound.port,0,128},[](const auto&,auto){});rebound.start();rebound.stop();
}
}
int main(){try{history_tests();unacked_tests();socket_tests("127.0.0.1");socket_tests("::1");}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}if(failures)return 1;std::cout<<"Packet history, reliable accounting, IPv4/IPv6 UDP tests passed\n";}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/xfer_download.hpp"
#include "opensim/scene/inventory_packets.hpp"
#include <iostream>
#include <bit>
using namespace opensim;using namespace scene;
namespace {
std::string hex(std::span<const std::uint8_t> bytes){std::string s;constexpr char h[]="0123456789abcdef";for(auto b:bytes){s+=h[b>>4];s+=h[b&15];}return s;}
XferBytes bytes(int size,int salt=0){auto data=std::make_shared<std::vector<std::uint8_t>>(size);for(int i=0;i<size;++i)(*data)[i]=static_cast<std::uint8_t>(i*17+salt);return data;}
std::string probe(int kind,int size,int burst,int ping,int mode){std::vector<std::string> trace;std::int32_t now=mode==5?INT32_MAX-1500:100;auto clock=[&]{return now;};auto advance=[&](int n){now=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now)+static_cast<std::uint32_t>(n));};auto sink=[&](auto id,auto packet,const auto& data,int offset,int length){trace.push_back("P"+hex(network::lludp::encode_datagram(xfer_packet(id,packet,data,offset,length))));};
 if(kind==3){XferFiles x(clock);x.add("a",bytes(size));x.add("b",bytes(2048,1));x.add("c",bytes(1,2));auto request=[&](std::string_view name,std::uint64_t id){x.request(name,id,burst,ping,[&]{return ping;},sink);};request("a",10);request("b",20);request("c",30);advance(12001);x.transfers_tick(now);x.abort(20);x.add("d",bytes(1025,3));request("d",40);advance(12001);x.transfers_tick(now);trace.push_back("S"+std::to_string(x.files())+","+std::to_string(x.transfers()));}
 else if(kind==2){trace.push_back("P"+hex(network::lludp::encode_datagram(task_inventory_reply(framework::Uuid("11223344-5566-7788-99aa-bbccddeeff00"),static_cast<std::int16_t>(burst),*bytes(size)))));}
 else if(kind==0){XferDownload x("contents",bytes(size),0xfedcba9876543210ull,burst,clock,[&]{return ping;},sink);x.start();int last=std::max(0,(size-1)>>10);auto ack=[&](std::uint32_t n){bool v=x.ack(n);trace.push_back(v?"A1":"A0");};auto tick=[&](int n){advance(n);bool v=x.check_time(now);trace.push_back(v?"T1":"T0");};
 if(mode==0||mode==5){for(std::uint32_t n=0;n<=static_cast<std::uint32_t>(last);++n)ack(n);ack(static_cast<std::uint32_t>(last)|0x80000000u);}
 else if(mode==1){ack(static_cast<std::uint32_t>(last)|0x80000000u);ack(last);}
 else if(mode==2){ack(0);ack(0);ack(0);tick(1000);tick(1);}
 else if(mode==3){tick(2000);tick(1);for(int n=0;n<5;++n)tick(12001);ack(0);}
 else if(mode==4){x.done();trace.push_back(x.deleted()?"D1":"D0");ack(0);tick(50000);}
 else if(mode==6){for(int n=0;n<3;++n){tick(12001);ack(n);}tick(12001);}
 else if(mode==7){ack(0x7ffffffe);tick(12001);}
 trace.push_back(x.deleted()?"Z1":"Z0");
 }else{XferFiles x(clock);x.add("a",bytes(size));if(mode!=0)x.add("a",bytes(size,3));x.add("b",bytes(1));auto state=[&]{trace.push_back("S"+std::to_string(x.files())+","+std::to_string(x.transfers()));};auto request=[&](std::string_view file,std::uint64_t id){x.request(file,id,burst,ping,[&]{return ping;},sink);};state();request("missing",10);state();request("a",10);state();request("b",10);state();
 if(mode==0||mode==1){for(std::uint32_t n=0;n<=static_cast<std::uint32_t>(std::max(0,(size-1)>>10));++n)x.ack(10,n);state();x.ack(10,std::max(0,(size-1)>>10));state();}
 else if(mode==2){x.abort(10);state();x.abort(10);state();}
 else{for(int i=0;i<7;++i){advance(12001);x.transfers_tick(now);state();}}
 request("a",11);state();request("b",12);state();advance(500000);x.expire(now);state();}
 std::string result;for(const auto& entry:trace){if(!result.empty())result+='|';result+=entry;}return result;
}
void require(bool value,const char* name){if(!value)throw std::runtime_error(name);}
void checks(){std::int32_t now=100;std::vector<network::lludp::Packet> sent;XferDownload x("a",bytes(1025),42,0,[&]{return now;},[]{return 20;},[&](auto id,auto packet,const auto& data,int offset,int length){sent.push_back(xfer_packet(id,packet,data,offset,length));});x.start();require(sent.size()==1,"initial window");require(!sent[0].reliable&&!sent[0].zerocoded,"controller provides reliability");auto first=std::get<std::vector<std::uint8_t>>(sent[0].block("DataPacket").at("Data"));require(first.size()==1028&&first[0]==1&&first[1]==4,"first prefix length");require(!x.ack(0)&&sent.size()==2,"ACK advances one packet");require(std::get<std::uint64_t>(sent[1].block("XferID").at("Packet"))==0x80000001u,"final flag");require(std::get<std::vector<std::uint8_t>>(sent[1].block("DataPacket").at("Data")).size()==1,"final bytes");require(!x.ack(1)&&!x.deleted(),"first final ACK retains transfer");require(x.ack(1)&&x.deleted(),"duplicate final ACK retires transfer");XferFiles files([&]{return now;});files.add("a",bytes(0));files.request("missing",1,0,20,[]{return 20;},[](auto,auto,const auto&,int,int){throw std::runtime_error("missing file sent");});require(files.transfers()==0,"missing file silent");}
}
int main(int argc,char**argv){try{if(argc==2&&std::string_view(argv[1])=="--probe"){int k,s,b,p,m;while(std::cin>>k>>s>>b>>p>>m)std::cout<<probe(k,s,b,p,m)<<'\n';return 0;}checks();std::cout<<"PASS: Xfer packet/chunk/ACK/registry checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

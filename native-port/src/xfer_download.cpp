// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/xfer_download.hpp"
#include <algorithm>
#include <bit>
#include <limits>
namespace opensim::scene {
namespace {
std::int32_t wrap_add(std::int32_t a,std::int32_t b){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));}
std::int32_t sub(std::int32_t a,std::int32_t b){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a)-static_cast<std::uint32_t>(b));}
std::int32_t mul(std::int32_t a,std::int32_t b){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a)*static_cast<std::uint32_t>(b));}
void little(std::vector<std::uint8_t>& v,std::uint32_t n){for(int i=0;i<4;++i)v.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
}
network::lludp::Packet xfer_packet(std::uint64_t id,std::uint32_t packet,std::span<const std::uint8_t> data,int offset,int length){
 if(offset<0||length<0||static_cast<std::size_t>(offset)>data.size()||static_cast<std::size_t>(length)>data.size()-static_cast<std::size_t>(offset)||length+(offset==0?4:0)>1200||data.size()>std::numeric_limits<std::int32_t>::max())throw std::out_of_range("Xfer packet data bounds");
 auto p=network::lludp::make_packet("SendXferPacket");p.reliable=false;p.zerocoded=false;p.block("XferID")["ID"]=id;p.block("XferID")["Packet"]=std::uint64_t(packet);std::vector<std::uint8_t> payload;if(offset==0)little(payload,static_cast<std::uint32_t>(data.size()));payload.insert(payload.end(),data.begin()+offset,data.begin()+offset+length);p.block("DataPacket")["Data"]=std::move(payload);return p;
}
XferDownload::XferDownload(std::string filename,XferBytes data,std::uint64_t id,int burst_size,XferClock clock,std::function<int()> ping,XferSink sink):filename_(std::move(filename)),data_(std::move(data)),id_(id),burst_size_(burst_size),clock_(std::move(clock)),ping_(std::move(ping)),sink_(std::move(sink)){
 if(!data_||data_->size()>std::numeric_limits<std::int32_t>::max()||!clock_||!ping_||!sink_)throw std::invalid_argument("Xfer download dependencies");
}
void XferDownload::done(){std::lock_guard lock(mutex_);if(!deleted_){data_.reset();deleted_=true;}}
bool XferDownload::deleted()const{std::lock_guard lock(mutex_);return deleted_;}
void XferDownload::start(){std::lock_guard lock(mutex_);if(!data_)throw std::runtime_error("Deleted Xfer data");auto size=static_cast<int>(data_->size());if(size==0){last_packet_=last_bytes_=burst_size_=0;}else{last_packet_=size>>10;last_bytes_=size&1023;if(last_bytes_==0){last_bytes_=1024;--last_packet_;}}last_ack_=last_sent_=-1;retries_=0;burst();}
void XferDownload::burst(){int first=wrap_add(last_ack_,1),end=std::min(wrap_add(first,burst_size_),last_packet_);while(first<=end){send(first);first=wrap_add(first,1);}last_ack_time_=wrap_add(clock_(),1000);}
void XferDownload::send(int packet){if(packet>last_packet_)return;if(packet<0||!data_)throw std::out_of_range("Xfer packet index");auto number=static_cast<std::uint32_t>(packet);int length=1024;if(packet==last_packet_){length=last_bytes_;number|=0x80000000u;}sink_(id_,number,*data_,packet<<10,length);last_sent_=packet;}
bool XferDownload::ack(std::uint32_t packet){std::lock_guard lock(mutex_);if(deleted_)return true;packet&=0x7fffffffu;if(last_ack_<static_cast<std::int64_t>(packet))last_ack_=static_cast<int>(packet);else if(last_ack_==last_packet_){done();return true;}last_ack_time_=clock_();retries_=0;send(wrap_add(last_sent_,1));return false;}
bool XferDownload::check_time(std::int32_t now){std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return false;if(!deleted_){int timeout=std::clamp(mul(5,ping_()),1000,10000);if(sub(now,last_ack_time_)>timeout){if(++retries_>4)done();else{burst_size_=sub(last_sent_,last_ack_);burst();}}}return deleted_;}
XferFiles::XferFiles(XferClock clock):clock_(std::move(clock)){if(!clock_)throw std::invalid_argument("Xfer clock");last_tick_=wrap_add(clock_(),30000);last_expire_=wrap_add(last_tick_,180000);}
bool XferFiles::add(std::string filename,XferBytes data){std::lock_guard lock(mutex_);auto now=clock_();auto it=files_.find(filename);if(it!=files_.end()){it->second.refs=wrap_add(it->second.refs,1);it->second.data=std::move(data);it->second.timestamp=now;}else files_[filename]=File{std::move(data),1,now};return true;}
void XferFiles::request(std::string_view filename,std::uint64_t id,int throttle,int ping,std::function<int()> current_ping,XferSink sink){std::lock_guard lock(mutex_);auto file=files_.find(std::string(filename));if(file==files_.end()||transfers_.contains(id))return;int count=mul(throttle>>10,ping)>>10;count=std::min(count,32);auto transaction=std::make_shared<XferDownload>(std::string(filename),file->second.data,id,count,clock_,std::move(current_ping),std::move(sink));transfers_[id]=transaction;transaction->start();if(file->second.refs==1)files_.erase(file->first);else file->second.refs=sub(file->second.refs,1);}
void XferFiles::ack(std::uint64_t id,std::uint32_t packet){std::lock_guard lock(mutex_);auto it=transfers_.find(id);if(it!=transfers_.end()&&it->second->ack(packet))transfers_.erase(id);}
void XferFiles::abort(std::uint64_t id){std::lock_guard lock(mutex_);auto it=transfers_.find(id);if(it!=transfers_.end()){it->second->done();transfers_.erase(id);}}
void XferFiles::transfers_tick(std::int32_t now){std::lock_guard lock(mutex_);std::vector<std::pair<std::uint64_t,std::shared_ptr<XferDownload>>> snapshot;for(const auto& entry:transfers_)snapshot.push_back(entry);for(const auto& [id,transfer]:snapshot)if(transfer->check_time(now))transfers_.erase(id);}
void XferFiles::expire(std::int32_t now){std::lock_guard lock(mutex_);if(sub(now,last_expire_)>120000){last_expire_=now;std::vector<std::string> expired;for(const auto& [name,file]:files_)if(file.refs==0&&sub(now,file.timestamp)>120000)expired.push_back(name);for(const auto& name:expired)files_.erase(name);}}
void XferFiles::heartbeat(){std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return;auto now=clock_();if(sub(now,last_tick_)>750){if(!transfers_.empty()||!files_.empty()){transfers_tick(now);expire(now);}last_tick_=now;}}
std::size_t XferFiles::files()const{std::lock_guard lock(mutex_);return files_.size();}
std::size_t XferFiles::transfers()const{std::lock_guard lock(mutex_);return transfers_.size();}
}

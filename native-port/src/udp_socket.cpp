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
#include "opensim/network/udp_socket.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace opensim::network {
namespace {
#ifdef _WIN32
using NativeSocket=SOCKET;
using AddressLength=int;
constexpr auto InvalidSocket=INVALID_SOCKET;
void close_socket(NativeSocket socket){closesocket(socket);}
int error(){return WSAGetLastError();}
bool temporary_error(int value){return value==WSAEWOULDBLOCK||value==WSAEINTR||value==WSAECONNRESET;}
void platform_init(){
    struct Runtime{Runtime(){WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("WSAStartup failed");}~Runtime(){WSACleanup();}};
    static Runtime runtime;
}
#else
using NativeSocket=int;
using AddressLength=socklen_t;
constexpr auto InvalidSocket=-1;
void close_socket(NativeSocket socket){close(socket);}
int error(){return errno;}
bool temporary_error(int value){return value==EAGAIN||value==EWOULDBLOCK||value==EINTR||value==ECONNREFUSED;}
void platform_init(){}
#endif
struct Address{sockaddr_storage value{};AddressLength length=0;};
UdpEndpoint endpoint(const sockaddr_storage& address){
    std::array<char,INET6_ADDRSTRLEN> text{};
    UdpEndpoint result;
    if(address.ss_family==AF_INET){const auto& a=reinterpret_cast<const sockaddr_in&>(address);inet_ntop(AF_INET,&a.sin_addr,text.data(),static_cast<unsigned>(text.size()));result.port=ntohs(a.sin_port);}
    else if(address.ss_family==AF_INET6){const auto& a=reinterpret_cast<const sockaddr_in6&>(address);inet_ntop(AF_INET6,&a.sin6_addr,text.data(),static_cast<unsigned>(text.size()));result.port=ntohs(a.sin6_port);result.scope_id=a.sin6_scope_id;}
    else throw std::invalid_argument("Unsupported UDP address family");
    result.address=text.data();return result;
}
Address address(const UdpEndpoint& endpoint){
    Address result;
    if(endpoint.address.find(':')==std::string::npos){
        auto& a=reinterpret_cast<sockaddr_in&>(result.value);a.sin_family=AF_INET;a.sin_port=htons(endpoint.port);
        if(inet_pton(AF_INET,endpoint.address.c_str(),&a.sin_addr)!=1)throw std::invalid_argument("Invalid IPv4 UDP endpoint");
        result.length=sizeof(sockaddr_in);
    }else{
        auto& a=reinterpret_cast<sockaddr_in6&>(result.value);a.sin6_family=AF_INET6;a.sin6_port=htons(endpoint.port);a.sin6_scope_id=endpoint.scope_id;
        if(inet_pton(AF_INET6,endpoint.address.c_str(),&a.sin6_addr)!=1)throw std::invalid_argument("Invalid IPv6 UDP endpoint");
        result.length=sizeof(sockaddr_in6);
    }
    return result;
}
}
std::string UdpEndpoint::str()const{return address.find(':')==std::string::npos?address+":"+std::to_string(port):"["+address+(scope_id?"%"+std::to_string(scope_id):"")+"]:"+std::to_string(port);}
std::size_t UdpEndpointHash::operator()(const UdpEndpoint& value)const noexcept{return std::hash<std::string>{}(value.address)^(static_cast<std::size_t>(value.port)<<1)^(static_cast<std::size_t>(value.scope_id)<<17);}
struct UdpSocket::Impl {
    UdpSocketOptions options;
    Receiver receive;
    NativeSocket socket=InvalidSocket;
    std::mutex mutex;
    std::thread thread;
    std::atomic<bool> running=false;
    std::atomic<std::uint64_t> received=0,sent=0,receive_errors=0,send_errors=0,handler_errors=0,oversized=0;
    bool started=false;
    void loop(){
        std::array<std::uint8_t,65536> data;
        while(running.load()){
#ifdef _WIN32
            fd_set readable;FD_ZERO(&readable);FD_SET(socket,&readable);timeval timeout{0,50000};
            const auto ready=select(0,&readable,nullptr,nullptr,&timeout);
#else
            pollfd readable{socket,POLLIN,0};const auto ready=poll(&readable,1,50);
#endif
            if(!running.load())break;
            if(ready<0){if(!temporary_error(error()))++receive_errors;continue;}
            if(ready==0)continue;
            sockaddr_storage remote{};AddressLength length=sizeof(remote);
            const auto count=recvfrom(socket,reinterpret_cast<char*>(data.data()),static_cast<int>(data.size()),0,reinterpret_cast<sockaddr*>(&remote),&length);
            if(count<0){if(!temporary_error(error()))++receive_errors;continue;}
            ++received;
            if(static_cast<std::size_t>(count)>options.maximum_datagram){++oversized;continue;}
            try{receive(endpoint(remote),std::span(data).first(static_cast<std::size_t>(count)));}catch(...){++handler_errors;}
        }
    }
};
UdpSocket::UdpSocket(UdpSocketOptions options,Receiver receive):impl_(std::make_unique<Impl>()){
    if(!receive||options.port<0||options.port>65535||options.maximum_datagram==0||options.maximum_datagram>65507||options.receive_buffer_bytes<0)throw std::invalid_argument("Invalid UDP socket configuration");
    platform_init();impl_->options=std::move(options);impl_->receive=std::move(receive);
}
UdpSocket::~UdpSocket(){stop();}
UdpEndpoint UdpSocket::start(){
    std::lock_guard lock(impl_->mutex);
    if(impl_->started)throw std::logic_error("UDP socket already started");
    Address local=address({impl_->options.address,static_cast<std::uint16_t>(impl_->options.port)});
    auto socket=::socket(local.value.ss_family,SOCK_DGRAM,IPPROTO_UDP);
    if(socket==InvalidSocket)throw std::runtime_error("Cannot create UDP socket");
    try{
#ifdef _WIN32
        const int exclusive=1;
        if(setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))!=0)throw std::runtime_error("Cannot set exclusive UDP binding");
        BOOL behavior=FALSE;DWORD returned=0;
        constexpr DWORD UdpConnectionResetControl=0x9800000cu;
        WSAIoctl(socket,UdpConnectionResetControl,&behavior,sizeof(behavior),nullptr,0,&returned,nullptr,nullptr);
        u_long nonblocking=1;if(ioctlsocket(socket,FIONBIO,&nonblocking)!=0)throw std::runtime_error("Cannot set nonblocking UDP socket");
#else
        const auto flags=fcntl(socket,F_GETFL,0);if(flags<0||fcntl(socket,F_SETFL,flags|O_NONBLOCK)<0)throw std::runtime_error("Cannot set nonblocking UDP socket");
#endif
        if(impl_->options.receive_buffer_bytes){const int size=impl_->options.receive_buffer_bytes;if(setsockopt(socket,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&size),sizeof(size))!=0)throw std::runtime_error("Cannot set UDP receive buffer");}
        if(local.value.ss_family==AF_INET){int ttl=0;AddressLength size=sizeof(ttl);if(getsockopt(socket,IPPROTO_IP,IP_TTL,reinterpret_cast<char*>(&ttl),&size)==0&&ttl<128){ttl=128;setsockopt(socket,IPPROTO_IP,IP_TTL,reinterpret_cast<const char*>(&ttl),sizeof(ttl));}}
        if(bind(socket,reinterpret_cast<sockaddr*>(&local.value),local.length)!=0)throw std::runtime_error("Cannot bind UDP listener");
        local.length=sizeof(local.value);if(getsockname(socket,reinterpret_cast<sockaddr*>(&local.value),&local.length)!=0)throw std::runtime_error("Cannot read bound UDP endpoint");
        impl_->socket=socket;impl_->running=true;impl_->started=true;
        try{impl_->thread=std::thread([this]{impl_->loop();});}catch(...){impl_->running=false;impl_->socket=InvalidSocket;throw;}
    }catch(...){close_socket(socket);throw;}
    return endpoint(local.value);
}
void UdpSocket::stop(){
    impl_->running=false;
    if(impl_->thread.joinable())impl_->thread.join();
    std::lock_guard lock(impl_->mutex);
    if(impl_->socket!=InvalidSocket){close_socket(impl_->socket);impl_->socket=InvalidSocket;}
}
bool UdpSocket::send_to(const UdpEndpoint& destination,std::span<const std::uint8_t> data){
    if(data.size()>impl_->options.maximum_datagram){++impl_->send_errors;return false;}
    const auto target=address(destination);
    std::lock_guard lock(impl_->mutex);
    if(!impl_->running.load()||impl_->socket==InvalidSocket)return false;
    const auto result=sendto(impl_->socket,reinterpret_cast<const char*>(data.data()),static_cast<int>(data.size()),0,reinterpret_cast<const sockaddr*>(&target.value),target.length);
    if(result<0||static_cast<std::size_t>(result)!=data.size()){++impl_->send_errors;return false;}
    ++impl_->sent;return true;
}
bool UdpSocket::is_running()const noexcept{return impl_->running.load();}
UdpSocketStatistics UdpSocket::statistics()const noexcept{return{impl_->received.load(),impl_->sent.load(),impl_->receive_errors.load(),impl_->send_errors.load(),impl_->handler_errors.load(),impl_->oversized.load()};}
}

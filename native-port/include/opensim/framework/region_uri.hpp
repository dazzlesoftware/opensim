/*
 * Copyright (c) Contributors, http://opensimulator.org/
 * See CONTRIBUTORS.TXT for a full list of copyright holders.
 *
 * - Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Neither the name of the openmetaverse.org nor the names
 *   of its contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
namespace opensim::framework {
bool valid_http_host(const std::string& host);
class RegionUri {
public:
    enum Flags : std::int32_t {HasHost=2,HasResolvedHost=4,HasUserName=8,HasUserPass=16,HasRegionName=32,HasCoords=64,IsLocalGrid=128};
    using Resolver=std::function<std::optional<std::string>(const std::string&)>;
    using LocalGrid=std::function<int(const std::string&)>;
    explicit RegionUri(const std::optional<std::string>& value);
    RegionUri(const std::optional<std::string>& value,const LocalGrid& grid,const Resolver& resolver={});
    void parse(const std::optional<std::string>& value);
    bool resolve_dns(const Resolver& resolver={});
    bool valid()const{return (flags&(HasHost|HasRegionName))!=0;}
    bool has_host()const{return (flags&HasHost)!=0;}
    bool has_region_name()const{return (flags&HasRegionName)!=0;}
    bool local()const{return (flags&IsLocalGrid)!=0;}
    void set_local(bool value);
    std::string host_url()const;
    std::string host_url_slash()const;
    std::string region_url_name()const;
    std::string host_port_space_name()const;
    std::int32_t flags=0,port=80,x=127,y=127,z=2;
    std::optional<std::string> original=std::string{},ip;
    std::string schema="http://",host,region_name,username,user_pass;
};
}

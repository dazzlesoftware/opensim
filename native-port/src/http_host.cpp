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
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/invariant_case.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "opensim/framework/config_uri_host_name.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/integer_text.hpp"
#include <stdexcept>
namespace opensim::framework {
HttpHost::HttpHost(const std::optional<std::string>& input, bool with_dns,
                   const RegionUri::Resolver& resolver) {
    if (!input || input->empty())
        return;
    flags = None;
    auto value = lower_invariant_culture(std::u16string_view(utf16_replacing(*input)));
    try {
        auto length = value.size();
        if (value.back() == u'/')
            --length;
        std::size_t start = 0;
        bool secure = false;
        if (invariant_starts_with(utf8(value), "http")) {
            secure = value.at(4) == u's';
            start = secure ? 8 : 7;
        }
        if (start > length)
            throw std::out_of_range("Invalid HTTP host span");
        auto colon = value.find(u':', start);
        bool explicit_port = colon != std::u16string::npos && colon < length && colon > 0;
        auto candidate = std::make_shared<const std::u16string>(
            value.substr(start, (explicit_port ? colon : length) - start));
        auto type = config_uri_check_host_name(candidate);
        if (type == ConfigUriHostNameType::Unknown || type == ConfigUriHostNameType::Basic)
            return;
        if (explicit_port) {
            std::uint32_t number;
            if (!try_parse_unsigned_integer(
                    utf8(std::u16string_view(value).substr(colon + 1, length - colon - 1)), number) ||
                number > 65535)
                return;
            port = static_cast<int>(number);
        } else
            port = secure ? 443 : 80;
        flags = ValidHost;
        host = utf8(*candidate);
        uri = std::string(secure ? "https://" : "http://") + host + ":" + std::to_string(port);
        if (with_dns) {
            RegionUri lookup("");
            lookup.flags = RegionUri::HasHost;
            lookup.host = host;
            if (lookup.resolve_dns(resolver)) {
                flags = ValidResolved;
                ip = lookup.ip;
            }
        }
    } catch (...) {
        flags = None;
        ip.reset();
        uri.clear();
    }
}
bool HttpHost::resolve_dns(const RegionUri::Resolver& resolver) {
    RegionUri lookup("");
    lookup.flags = RegionUri::HasHost;
    lookup.host = host;
    if (!lookup.resolve_dns(resolver)) {
        flags = static_cast<unsigned char>(flags & ~Resolved);
        return false;
    }
    flags = static_cast<unsigned char>(flags | Resolved);
    return true;
}
bool HttpHost::valid_and_resolved(std::string& error) const {
    if (!valid()) {
        error = "failed to parse uri";
        return false;
    }
    if (!resolved()) {
        error = "failed DNS resolve of uri host";
        return false;
    }
    error.clear();
    return true;
}
int HttpHost::compare(const HttpHost& other) const {
    if (port != other.port || ((flags & other.flags) & ValidHost) == 0)
        return -1;
    return invariant_compare(host, other.host);
}
}

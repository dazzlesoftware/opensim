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
#include "opensim/framework/http_uri.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/invariant_case.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::framework {
HttpUri::HttpUri(const std::optional<std::string>& input, bool with_dns, const RegionUri::Resolver& resolver)
    : HttpHost(std::nullopt) {
    port = -1;
    if (!input || input->empty())
        return;
    flags = None;
    try {
#ifdef _WIN32
        constexpr bool windows = true;
#else
        constexpr bool windows = false;
#endif
        auto object = config_generic_uri_create(
            std::make_shared<const std::u16string>(utf16_replacing(*input)), ConfigUriObjectKind::Absolute,
            native_config_uri_components_backend(), windows);
        const auto scheme = *object->scheme();
        if (scheme != u"http" && scheme != u"https")
            return;
        flags = ValidHost;
        host = utf8(lower_invariant_culture(
            *config_generic_uri_component(object, ConfigGenericUriComponent::DnsSafeHost)));
        port = config_generic_uri_port(object);
        path = utf8(*config_generic_uri_component(object, ConfigGenericUriComponent::AbsolutePath));
        url = utf8(scheme) + "://" + host + ":" + std::to_string(port);
        uri = utf8(*object->absolute_uri());
        if (with_dns) {
            RegionUri lookup("");
            lookup.flags = RegionUri::HasHost;
            lookup.host = host;
            if (lookup.resolve_dns(resolver)) {
                ip = lookup.ip;
                flags = ValidResolved;
            }
        }
    } catch (...) {
        flags = None;
        ip.reset();
        uri.clear();
    }
}
}

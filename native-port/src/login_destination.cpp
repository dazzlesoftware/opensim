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
#include "opensim/services/login_destination.hpp"
#include "opensim/framework/vector_text.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include "opensim/framework/dotnet_regex_interpreter.hpp"
#include "opensim/framework/execution_stack.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
namespace opensim::services {
namespace {
std::array<std::string, 4> uri_fields(const std::string& value) {
    using namespace framework;
    struct Matcher {
        std::mutex mutex;
        DotnetRegexNodeText expression = std::make_shared<const std::u16string>(
            uR"(^uri:(?<region>[^&]+)&(?<x>\d+[.]?\d*)&(?<y>\d+[.]?\d*)&(?<z>\d+[.]?\d*)$)");
        DotnetRegexInterpreter runner{
            dotnet_regex_parse(
                expression, 8, [] { return std::u16string{}; }, sufficient_execution_stack),
            expression, sufficient_execution_stack};
    };
    static Matcher matcher;
    std::lock_guard lock(matcher.mutex);
    auto input = std::make_shared<const std::u16string>(utf16(value));
    const auto match = matcher.runner.scan(input, 0, static_cast<std::int32_t>(input->size()), 0);
    std::array<std::string, 4> fields{};
    if (match.success)
        for (const auto& group : match.groups) {
            const auto slot = group.name == u"region" ? 0
                              : group.name == u"x"    ? 1
                              : group.name == u"y"    ? 2
                              : group.name == u"z"    ? 3
                                                      : -1;
            if (slot >= 0 && group.success)
                fields[slot] = utf8(std::u16string_view(*input).substr(group.index, group.length));
        }
    return fields;
}
float clamp(float value, std::int32_t size) {
    const float upper = static_cast<float>(size) - 0.5f;
    if (upper < 0.5f)
        throw std::invalid_argument("Invalid region clamp bounds");
    return std::clamp(value, 0.5f, upper);
}
}
LoginDestination LoginDestinationSelector::find(const framework::Uuid& scope,
                                                const std::optional<GridUserInfo>& info,
                                                const std::optional<std::string>& start,
                                                const std::optional<GridRegion>& home) {
    constexpr std::uint32_t via_home = 32, via_region = 4096;
    LoginDestination result;
    if (!grid_)
        return result;
    if (!start)
        throw std::invalid_argument("Null login start location");
    if (*start == "home") {
        if (!info)
            return result;
        if (home) {
            result.position = info->home_position;
            result.look_at = info->home_look_at;
            result.flags |= via_home;
            result.region = home;
            return result;
        }
        const auto defaults = grid_->defaults(scope);
        result.region = defaults.empty() ? alternative(scope) : std::optional{defaults.front()};
        if (result.region) {
            result.flags |= via_region;
            result.where = "safe";
        }
        return result;
    }
    if (*start == "last") {
        result.where = "last";
        if (!info)
            return result;
        if (!info->last_region.is_zero())
            result.region = grid_->by_id(scope, info->last_region);
        if (!result.region) {
            const auto defaults = grid_->defaults(scope);
            result.region = defaults.empty() ? alternative(scope) : std::optional{defaults.front()};
            if (result.region) {
                result.flags |= via_region;
                result.where = "safe";
            }
        } else {
            result.position = info->last_position;
            result.position.x = clamp(result.position.x, result.region->size_x);
            result.position.y = clamp(result.position.y, result.region->size_y);
            result.look_at = info->last_look_at;
        }
        return result;
    }
    result.flags |= via_region;
    result.where = "url";
    // The source locks a shared regex, then parses its named captures. An
    // unsuccessful match yields empty captures and fails numeric parsing.
    const auto fields = uri_fields(*start);
    if (!framework::parse_invariant_float(fields[1], result.position.x) ||
        !framework::parse_invariant_float(fields[2], result.position.y) ||
        !framework::parse_invariant_float(fields[3], result.position.z))
        throw std::invalid_argument("Invalid login start coordinates");
    const auto name = std::string(fields[0]);
    // LLLoginService disables direct Hypergrid login through custom start URIs.
    if (name.find('@') != std::string::npos)
        return result;
    result.region = grid_->by_name(scope, name);
    if (result.region)
        return result;
    const auto defaults = grid_->defaults(scope);
    result.region = defaults.empty() ? alternative(scope) : std::optional{defaults.front()};
    if (result.region)
        result.where = "safe";
    return result;
}
std::optional<GridRegion> LoginDestinationSelector::alternative(const framework::Uuid& scope) {
    const auto fallback = grid_->fallback(scope, 256000, 256000);
    if (!fallback.empty())
        return fallback.front();
    if (fallback_to_any_) {
        const auto online = grid_->online(scope, 256000, 256000, 10);
        if (!online.empty())
            return online.front();
    }
    return std::nullopt;
}
}

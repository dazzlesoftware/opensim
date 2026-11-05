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
#pragma once
#include "opensim/framework/uuid.hpp"
#include <map>
#include <memory>
#include <optional>
#include <vector>
namespace opensim::services {
using RegionFields = std::map<std::string, std::optional<std::string>>;
using OrderedRegionFields = std::vector<std::pair<std::string, std::optional<std::string>>>;
enum RegionFlags : std::int32_t {
    DefaultRegion = 1,
    FallbackRegion = 2,
    RegionOnline = 4,
    NoDirectLogin = 8,
    Persistent = 16,
    LockedOut = 32,
    NoMove = 64,
    Reservation = 128,
    Authenticate = 256,
    Hyperlink = 512,
    DefaultHGRegion = 1024
};
struct RegionEndpoint {
    std::string address = "0.0.0.0";
    std::uint16_t port = 0;
};
// GridRegion's IPEndPoint is a reference object in the original model. Copies
// share mutations; assigning a new endpoint replaces only that region's slot.
class RegionEndpointRef {
  public:
    RegionEndpointRef() = default;
    RegionEndpointRef(std::nullopt_t) {}
    RegionEndpointRef(RegionEndpoint value) : value_(std::make_shared<RegionEndpoint>(std::move(value))) {}
    explicit operator bool() const noexcept {
        return static_cast<bool>(value_);
    }
    bool has_value() const noexcept {
        return static_cast<bool>(value_);
    }
    RegionEndpoint* operator->() const noexcept {
        return value_.get();
    }
    RegionEndpoint& operator*() const {
        return *value_;
    }
    void reset() noexcept {
        value_.reset();
    }

  private:
    std::shared_ptr<RegionEndpoint> value_;
};
struct GridRegion {
    framework::Uuid id, scope, terrain_image, parcel_image, estate_owner;
    std::int32_t x = 0, y = 0, size_x = 256, size_y = 256, maturity = 0;
    std::uint32_t http_port = 0;
    std::uint8_t access = 0;
    std::optional<std::int32_t> flags;
    std::optional<std::string> name = std::string{}, external_host, region_secret = std::string{},
                               raw_server_uri = std::string{};
    std::string token;
    RegionEndpointRef internal_endpoint;
    static GridRegion from_fields(const RegionFields& fields);
    static GridRegion from_cells(std::uint32_t x, std::uint32_t y);
    GridRegion legacy_copy() const;
    std::string server_uri() const;
    void set_server_uri(const std::optional<std::string>& value);
    OrderedRegionFields fields() const;
    std::uint64_t handle() const;
    std::int32_t coord_x() const;
    std::int32_t coord_y() const;
    bool equals(const GridRegion& other) const {
        return !id.is_zero() && id == other.id;
    }
};
struct RegionData {
    framework::Uuid id, scope;
    std::optional<std::string> name;
    std::int32_t x = 0, y = 0, size_x = 0, size_y = 0;
    RegionFields data;
};
RegionData region_data(const GridRegion& region);
GridRegion grid_region(const RegionData& data);
std::int32_t parse_region_integer(const std::optional<std::string>& value);
std::int32_t parse_region_flags(std::int32_t previous, std::string_view specification);
int compare_region_distance(const RegionData& a, const RegionData& b, std::int32_t x, std::int32_t y);
}

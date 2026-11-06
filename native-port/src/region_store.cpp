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
#include "opensim/services/region_store.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
namespace opensim::services {
namespace {
std::string lower(std::string_view value) {
    return framework::utf8(framework::invariant_lower_utf16(framework::utf16(value)));
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
const std::string& name(const RegionData& row) {
    if (!row.name)
        throw std::invalid_argument("Null region name");
    return *row.name;
}
}
struct NullRegionStore::State {
    struct Slot {
        framework::Uuid key;
        std::shared_ptr<RegionData> row;
    };
    std::recursive_mutex mutex;
    std::vector<Slot> slots;
    std::vector<std::size_t> free;
    std::unordered_map<framework::Uuid, std::size_t, framework::UuidHash> index;
};
std::shared_ptr<NullRegionStore::State> NullRegionStore::singleton(bool create) {
    static std::mutex mutex;
    static std::shared_ptr<State> value;
    std::lock_guard lock(mutex);
    if (create && !value)
        value = std::make_shared<State>();
    return value;
}
NullRegionStore::NullRegionStore(const std::string& connection) {
    state_ = connection == "!static" ? std::make_shared<State>() : singleton(true);
}
std::shared_ptr<RegionData> NullRegionStore::get(const framework::Uuid& id, const framework::Uuid&) {
    std::lock_guard lock(state_->mutex);
    const auto found = state_->index.find(id);
    return found == state_->index.end() ? nullptr : state_->slots[found->second].row;
}
std::shared_ptr<RegionData> NullRegionStore::at(std::int32_t x, std::int32_t y, const framework::Uuid&) {
    std::lock_guard lock(state_->mutex);
    for (const auto& slot : state_->slots)
        if (slot.row) {
            const auto& row = *slot.row;
            if (x >= row.x && x < add(row.x, row.size_x) && y >= row.y && y < add(row.y, row.size_y))
                return slot.row;
        }
    return nullptr;
}
std::shared_ptr<RegionData> NullRegionStore::specific(const std::string& wanted, const framework::Uuid&) {
    std::lock_guard lock(state_->mutex);
    for (const auto& slot : state_->slots)
        if (slot.row && framework::invariant_equals_ignore_case(name(*slot.row), wanted))
            return slot.row;
    return nullptr;
}
std::optional<RegionRows> NullRegionStore::find(const std::string& pattern, const framework::Uuid&) {
    auto clean = lower(pattern);
    bool prefix = false, suffix = false;
    if (clean == "%") {
        prefix = suffix = true;
        clean.clear();
    } else {
        if (clean.starts_with('%')) {
            prefix = true;
            clean.erase(0, 1);
        }
        if (pattern.ends_with('%')) {
            suffix = true;
            if (clean.empty())
                throw std::out_of_range("Empty wildcard");
            clean.pop_back();
        }
    }
    std::lock_guard lock(state_->mutex);
    RegionRows result;
    for (const auto& slot : state_->slots)
        if (slot.row) {
            const auto value = lower(name(*slot.row));
            if (prefix && suffix ? value.find(clean) != std::string::npos
                : suffix         ? framework::invariant_starts_with(value, clean)
                : prefix         ? framework::invariant_ends_with_utf16(framework::utf16(value),
                                                                       framework::utf16(clean))
                                 : value == clean)
                result.push_back(slot.row);
        }
    return result.empty() ? std::nullopt : std::optional{result};
}
RegionRows NullRegionStore::range(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2,
                                  const framework::Uuid&) {
    std::lock_guard lock(state_->mutex);
    RegionRows result;
    for (const auto& slot : state_->slots)
        if (slot.row) {
            const auto& row = *slot.row;
            if (add(row.x, row.size_x) > x1 && row.x <= x2 && add(row.y, row.size_x) > y1 && row.y <= y2)
                result.push_back(slot.row);
        }
    return result;
}
RegionRows NullRegionStore::with_flags(std::int32_t flags, const framework::Uuid&) {
    // The original private flags query always delegates to Instance, even for
    // !static stores. Preserve both that alias and the absent-instance failure.
    const auto shared = singleton();
    if (!shared)
        throw std::runtime_error("Null region singleton");
    std::lock_guard lock(shared->mutex);
    RegionRows result;
    for (const auto& slot : shared->slots)
        if (slot.row && (parse_region_integer(slot.row->data.at("flags")) & flags) != 0)
            result.push_back(slot.row);
    return result;
}
bool NullRegionStore::store(const std::shared_ptr<RegionData>& row) {
    if (!row)
        throw std::invalid_argument("Missing region data");
    std::lock_guard lock(state_->mutex);
    const auto found = state_->index.find(row->id);
    if (found != state_->index.end())
        state_->slots[found->second].row = row;
    else {
        std::size_t index;
        if (state_->free.empty()) {
            index = state_->slots.size();
            state_->slots.push_back({row->id, row});
        } else {
            index = state_->free.back();
            state_->free.pop_back();
            state_->slots[index] = {row->id, row};
        }
        state_->index[row->id] = index;
    }
    return true;
}
bool NullRegionStore::set(const framework::Uuid& id, const std::string& item, const std::string& value) {
    std::lock_guard lock(state_->mutex);
    auto row = get(id, {});
    if (!row)
        return false;
    row->data[item] = value;
    return true;
}
bool NullRegionStore::erase(const framework::Uuid& id) {
    std::lock_guard lock(state_->mutex);
    const auto found = state_->index.find(id);
    if (found == state_->index.end())
        return false;
    state_->slots[found->second].row.reset();
    state_->free.push_back(found->second);
    state_->index.erase(found);
    return true;
}
}

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
#include "opensim/services/region_store.hpp"
#include <stdexcept>
class FaultRegionStore : public opensim::services::RegionStore {
    opensim::services::NullRegionStore inner;

public:
    bool store_false = false, store_throws = false, erase_false = false, erase_throws = false;
    std::shared_ptr<opensim::services::RegionData> get(const opensim::framework::Uuid& id,
                                                       const opensim::framework::Uuid& scope) override {
        return inner.get(id, scope);
    }
    std::shared_ptr<opensim::services::RegionData> at(std::int32_t x, std::int32_t y,
                                                      const opensim::framework::Uuid& scope) override {
        return inner.at(x, y, scope);
    }
    std::shared_ptr<opensim::services::RegionData> specific(const std::string& name,
                                                            const opensim::framework::Uuid& scope) override {
        return inner.specific(name, scope);
    }
    std::optional<opensim::services::RegionRows> find(const std::string& pattern,
                                                      const opensim::framework::Uuid& scope) override {
        return inner.find(pattern, scope);
    }
    opensim::services::RegionRows range(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2,
                                        const opensim::framework::Uuid& scope) override {
        return inner.range(x1, y1, x2, y2, scope);
    }
    opensim::services::RegionRows with_flags(std::int32_t flags,
                                             const opensim::framework::Uuid& scope) override {
        return inner.with_flags(flags, scope);
    }
    bool store(const std::shared_ptr<opensim::services::RegionData>& row) override {
        if (store_throws)
            throw std::runtime_error("fixture write");
        return !store_false && inner.store(row);
    }
    bool set(const opensim::framework::Uuid& id, const std::string& key, const std::string& value) override {
        return inner.set(id, key, value);
    }
    bool erase(const opensim::framework::Uuid& id) override {
        if (erase_throws)
            throw std::runtime_error("fixture delete");
        return !erase_false && inner.erase(id);
    }
};

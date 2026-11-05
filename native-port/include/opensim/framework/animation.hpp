// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/osd_value.hpp"
#include <functional>
namespace opensim::framework {
class Animation {
  public:
    Uuid animation_id, object_id;
    std::int32_t sequence_number = 0;
    Animation() = default;
    virtual ~Animation() = default;
    Animation(const Uuid& animation, std::int32_t sequence, const Uuid& object)
        : animation_id(animation), object_id(object), sequence_number(sequence) {}
    explicit Animation(const network::Value* data) {
        unpack(data);
    }
    Animation(const Animation&) = delete;
    Animation& operator=(const Animation&) = delete;
    network::Value pack() const;
    void unpack(const network::Value* data);
    void unpack(const network::Value& data) {
        unpack(&data);
    }
    virtual bool equals(const Animation* other) const noexcept;
    bool operator==(const Animation& other) const noexcept {
        return equals(&other);
    }
    // Source GetHashCode uses object identity despite value-based Equals.
    virtual std::size_t identity_hash() const noexcept {
        return std::hash<const void*>{}(this);
    }
    virtual std::string str() const;
};
}

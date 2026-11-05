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
#include <cstdint>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace opensim::framework {
class AssetDateTime;
class ConfigUriObject;
}
namespace opensim::network {
class BinaryStorage {
  public:
    using Bytes = std::shared_ptr<std::vector<std::uint8_t>>;
    BinaryStorage() noexcept = default;
    BinaryStorage(const BinaryStorage& other) noexcept : bytes_(other.load()) {}
    BinaryStorage& operator=(const BinaryStorage& other) noexcept {
        bytes_.store(other.load());
        return *this;
    }
    BinaryStorage& operator=(Bytes value) noexcept {
        bytes_.store(std::move(value));
        return *this;
    }
    Bytes load() const noexcept {
        return bytes_.load();
    }
    bool initialize(Bytes& expected, Bytes desired) const noexcept {
        return bytes_.compare_exchange_strong(expected, std::move(desired));
    }

  private:
    mutable std::atomic<Bytes> bytes_{};
};
struct Value {
    enum class Kind {
        Undefined,
        String,
        Integer,
        Boolean,
        Real,
        Uuid,
        Date,
        Uri,
        Binary,
        Array,
        Map,
        Long,
        NullReference
    };
    Kind kind = Kind::Undefined;
    std::string text;
    std::int32_t integer = 0;
    std::int64_t long_value = 0;
    bool boolean = false;
    double real = 0;
    mutable BinaryStorage binary_data;
    std::shared_ptr<const std::u16string> sdk_string;
    std::shared_ptr<const framework::AssetDateTime> sdk_date;
    std::shared_ptr<const framework::ConfigUriObject> sdk_uri;
    std::vector<Value> array;
    std::map<std::string, Value> map;
    std::vector<std::string> map_order;
    std::shared_ptr<Value> reference;
    const Value& view() const;
    static Value alias(const std::shared_ptr<Value>& value);
    Value() = default;
    Value(const char* value) : kind(Kind::String), text(value) {}
    Value(std::string value) : kind(Kind::String), text(std::move(value)) {}
    explicit Value(std::int32_t value) : kind(Kind::Integer), integer(value) {}
    explicit Value(bool value) : kind(Kind::Boolean), boolean(value) {}
    static Value object(std::map<std::string, Value> fields);
    static Value null_reference() {
        Value value;
        value.kind = Kind::NullReference;
        return value;
    }
    static Value long_integer(std::int64_t number) {
        Value result;
        result.kind = Kind::Long;
        result.long_value = number;
        return result;
    }
    std::string as_string() const;
    bool contains(std::string_view name) const;
    const Value& get(std::string_view name) const;
    void set(std::string key, Value value);
    std::vector<std::string> ordered_keys() const;
};
struct RpcCall {
    std::string method;
    std::vector<Value> parameters;
};
RpcCall parse_xml_rpc(std::string_view bytes);
std::string xml_rpc_request(const RpcCall& call);
Value parse_xml_rpc_response(std::string_view bytes);
Value parse_llsd_xml(std::string_view bytes);
std::string xml_rpc_response(const Value& value);
std::string xml_rpc_fault(std::int32_t code, const std::string& message);
std::string llsd_xml(const Value& value);
std::string grid_info_xml(const std::map<std::string, std::string>& info);
std::string json_strings(const std::map<std::string, std::string>& fields);
}

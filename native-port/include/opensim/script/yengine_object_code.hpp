// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opensim::framework {
class Uuid;
}

namespace opensim::script {
// ScriptObjWriter's portable records, rather than native CLR instruction offsets.
enum class YEngineObjectRecordKind : std::uint8_t {
    BeginMethod,
    EndMethod,
    End,
    DeclareLabel,
    DeclareLocal,
    DeclareMethod,
    MarkLabel,
    EmitNone,
    EmitField,
    EmitLocal,
    EmitType,
    EmitLabel,
    EmitExternalMethod,
    EmitInternalMethod,
    EmitConstructor,
    EmitDouble,
    EmitFloat,
    EmitInteger,
    EmitString,
    EmitLabels,
    BeginException,
    BeginCatch,
    BeginFinally,
    EndException
};

struct YEngineObjectLocation {
    std::string file;
    std::int32_t line = 0, column = 0;
};

struct YEngineObjectRecord {
    YEngineObjectRecordKind kind;
    std::optional<std::int16_t> opcode;
    YEngineObjectLocation location;
    std::vector<std::string> strings;
    std::vector<std::int32_t> integers;
    std::optional<std::uint64_t> floating_bits;
};

struct YEngineObjectVariable {
    std::string name, type;
};

struct YEngineObjectMethod {
    std::string return_type;
    bool has_body = false;
    std::vector<YEngineObjectVariable> arguments;
    std::map<std::int32_t, YEngineObjectVariable> locals;
    std::map<std::int32_t, std::string> labels;
    std::map<std::int32_t, std::size_t> label_records;
    std::vector<YEngineObjectRecord> records;
};

struct YEngineObjectGlobal {
    std::string name, type;
    std::int32_t index;
};

struct YEngineObjectBinding {
    std::int32_t virtual_index;
    std::string method, signature;
};

struct YEngineObjectInterface {
    std::string name;
    std::vector<YEngineObjectBinding> methods;
};

struct YEngineObjectType {
    YEngineObjectLocation location;
    std::uint8_t kind;
    std::int32_t index;
    std::string return_type;
    std::vector<std::string> argument_types;
    std::array<std::int32_t, 11> instance_sizes{};
    std::int32_t virtual_methods = 0, array_rank = 0;
    std::string base_type, element_type;
    std::vector<YEngineObjectBinding> bindings;
    std::vector<YEngineObjectInterface> interfaces;
};

struct YEngineObjectCode {
    std::int32_t compiled_version;
    std::string source_hash;
    std::array<std::int32_t, 11> global_sizes;
    std::vector<std::string> states;
    std::vector<YEngineObjectGlobal> globals;
    std::map<std::string, YEngineObjectType> types;
    std::map<std::string, YEngineObjectMethod> methods;
};

// Reads the original -11 portable format, including script-defined type tables.
// Preserves operands and source locations without executing or verifying CLR code.
std::optional<YEngineObjectCode> decode_yengine_object_code(std::span<const std::uint8_t>);
std::string yengine_object_cache_key(std::string_view source, const framework::Uuid &asset);
std::optional<YEngineObjectCode> read_yengine_object_cache(const std::filesystem::path &directory,
                                                           const framework::Uuid &asset, std::string_view source,
                                                           std::string_view source_hash);
} // namespace opensim::script

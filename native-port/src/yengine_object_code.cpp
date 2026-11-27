// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/framework/uuid.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace opensim::script {
namespace {
class ObjectReader {
    using Kind = YEngineObjectRecordKind;
    std::span<const std::uint8_t> remaining_;
    std::size_t entries_ = 1000000;
    std::string source_file_;

    [[noreturn]] static void invalid() {
        throw std::invalid_argument("Unsupported YEngine object metadata");
    }

    std::span<const std::uint8_t> take(std::size_t size) {
        if (size > remaining_.size())
            invalid();
        const auto bytes = remaining_.first(size);
        remaining_ = remaining_.subspan(size);
        return bytes;
    }

    template <class T> T number() {
        using Bits = std::conditional_t<sizeof(T) == 8, std::uint64_t,
                                        std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint16_t>>;
        Bits value = 0;
        unsigned shift = 0;
        for (const auto byte : take(sizeof(T))) {
            value |= static_cast<Bits>(byte) << shift;
            shift += 8;
        }
        return std::bit_cast<T>(value);
    }

    void entry() {
        if (!entries_--)
            invalid();
    }

    std::int32_t index() {
        const auto value = number<std::int32_t>();
        if (value < 0 || value > 1000000)
            invalid();
        return value;
    }

    std::string text() {
        entry();
        std::uint32_t size = 0;
        for (unsigned shift = 0; shift <= 28; shift += 7) {
            const auto byte = take(1).front();
            if (shift == 28 && byte > 7)
                invalid();
            size |= static_cast<std::uint32_t>(byte & 127) << shift;
            if (!(byte & 128)) {
                const auto bytes = take(size);
                return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
            }
        }
        invalid();
    }

    void opcode(YEngineObjectRecord &record) {
        auto file = text();
        if (!file.empty())
            source_file_ = std::move(file);
        record.location = {source_file_, number<std::int32_t>(), number<std::int32_t>()};
        record.opcode = number<std::int16_t>();
    }

    void argument_types(YEngineObjectRecord &record) {
        const auto size = index();
        if (static_cast<std::size_t>(size) > remaining_.size())
            invalid();
        for (int i = 0; i < size; ++i)
            record.strings.push_back(text());
    }

    YEngineObjectBinding binding(std::int32_t virtual_index) {
        return {virtual_index, text(), text()};
    }

    YEngineObjectType type() {
        YEngineObjectType value{};
        value.location = {text(), number<std::int32_t>(), number<std::int32_t>()};
        value.kind = take(1).front();
        value.index = index();
        switch (value.kind) {
        case 0: { // TokenDeclSDTypeClass
            for (auto &size : value.instance_sizes)
                size = index();
            value.virtual_methods = index();
            value.base_type = text();
            value.array_rank = index();
            if (value.array_rank)
                value.element_type = text();
            for (;;) {
                const auto slot = number<std::int32_t>();
                if (slot < 0)
                    break;
                entry();
                value.bindings.push_back(binding(slot));
            }
            const auto count = index();
            if (static_cast<std::size_t>(count) > remaining_.size())
                invalid();
            for (int i = 0; i < count; ++i) {
                YEngineObjectInterface implementation;
                implementation.name = text();
                const auto methods = index();
                if (static_cast<std::size_t>(methods) > remaining_.size() / 6)
                    invalid();
                for (int j = 0; j < methods; ++j) {
                    entry();
                    implementation.methods.push_back(binding(number<std::int32_t>()));
                }
                value.interfaces.push_back(std::move(implementation));
            }
            break;
        }
        case 1: { // TokenDeclSDTypeDelegate (also generated for ordinary functions)
            value.return_type = text();
            const auto count = index();
            if (static_cast<std::size_t>(count) > remaining_.size())
                invalid();
            for (int i = 0; i < count; ++i)
                value.argument_types.push_back(text());
            break;
        }
        case 2: // TokenDeclSDTypeInterface
        case 3: // TokenDeclSDTypeTypedef
            break;
        default:
            invalid();
        }
        return value;
    }

  public:
    explicit ObjectReader(std::span<const std::uint8_t> bytes) : remaining_(bytes) {}

    YEngineObjectCode read() {
        constexpr std::string_view magic = "YObjectCode";
        const auto prefix = take(magic.size());
        if (!std::equal(prefix.begin(), prefix.end(), magic.begin()))
            invalid();
        YEngineObjectCode code{};
        code.compiled_version = number<std::int32_t>();
        if (code.compiled_version != -11)
            invalid();
        code.source_hash = text();
        for (auto &size : code.global_sizes)
            size = index();
        const auto states = index();
        if (static_cast<std::size_t>(states) > remaining_.size())
            invalid();
        for (int i = 0; i < states; ++i)
            code.states.push_back(text());
        for (;;) {
            auto name = text();
            if (name.empty())
                break;
            auto type = text();
            code.globals.push_back({std::move(name), std::move(type), index()});
        }
        for (;;) {
            auto name = text();
            if (name.empty())
                break;
            auto declaration = type();
            if (!code.types.emplace(std::move(name), std::move(declaration)).second)
                invalid();
        }
        YEngineObjectMethod *method = nullptr;
        std::set<std::string> completed;
        unsigned exceptions = 0;
        for (;;) {
            entry();
            const auto kind = static_cast<Kind>(take(1).front());
            if (kind == Kind::End) {
                if (method || !remaining_.empty())
                    invalid();
                return code;
            }
            if (kind == Kind::DeclareMethod) {
                if (method)
                    invalid();
                auto name = text();
                YEngineObjectMethod declaration;
                declaration.return_type = text();
                const auto arguments = index();
                if (static_cast<std::size_t>(arguments) > remaining_.size() / 2)
                    invalid();
                for (int i = 0; i < arguments; ++i) {
                    auto type = text();
                    declaration.arguments.push_back({text(), std::move(type)});
                }
                if (!code.methods.emplace(std::move(name), std::move(declaration)).second)
                    invalid();
                continue;
            }
            if (kind == Kind::BeginMethod) {
                if (method)
                    invalid();
                const auto name = text();
                const auto found = code.methods.find(name);
                if (found == code.methods.end() || !completed.insert(name).second)
                    invalid();
                method = &found->second;
                method->has_body = true;
                exceptions = 0;
                continue;
            }
            if (kind == Kind::EndMethod) {
                if (!method || exceptions)
                    invalid();
                method = nullptr;
                continue;
            }
            if (!method)
                invalid();
            YEngineObjectRecord record{kind, {}, {}, {}, {}, {}};
            switch (kind) {
            case Kind::DeclareLabel: {
                const auto value = index();
                auto name = text();
                if (!method->labels.emplace(value, name).second)
                    invalid();
                record.integers.push_back(value);
                record.strings.push_back(std::move(name));
                break;
            }
            case Kind::DeclareLocal: {
                const auto value = index();
                auto name = text();
                auto type = text();
                if (!method->locals.emplace(value, YEngineObjectVariable{name, type}).second)
                    invalid();
                record.integers.push_back(value);
                record.strings = {std::move(name), std::move(type)};
                break;
            }
            case Kind::MarkLabel: {
                const auto value = index();
                if (!method->labels.contains(value) ||
                    !method->label_records.emplace(value, method->records.size()).second)
                    invalid();
                record.integers.push_back(value);
                break;
            }
            case Kind::BeginException:
                ++exceptions;
                break;
            case Kind::BeginCatch:
                if (!exceptions)
                    invalid();
                record.strings.push_back(text());
                break;
            case Kind::BeginFinally:
                if (!exceptions)
                    invalid();
                break;
            case Kind::EndException:
                if (!exceptions)
                    invalid();
                --exceptions;
                break;
            case Kind::EmitNone:
                opcode(record);
                break;
            case Kind::EmitField:
                opcode(record);
                record.strings.push_back(text());
                record.strings.push_back(text());
                break;
            case Kind::EmitLocal: {
                opcode(record);
                const auto value = index();
                if (!method->locals.contains(value))
                    invalid();
                record.integers.push_back(value);
                break;
            }
            case Kind::EmitType:
            case Kind::EmitString:
                opcode(record);
                record.strings.push_back(text());
                break;
            case Kind::EmitLabel: {
                opcode(record);
                const auto value = index();
                if (!method->labels.contains(value))
                    invalid();
                record.integers.push_back(value);
                break;
            }
            case Kind::EmitLabels: {
                opcode(record);
                const auto count = index();
                if (static_cast<std::size_t>(count) > remaining_.size() / 4)
                    invalid();
                for (int i = 0; i < count; ++i) {
                    const auto value = index();
                    if (!method->labels.contains(value))
                        invalid();
                    record.integers.push_back(value);
                }
                break;
            }
            case Kind::EmitExternalMethod:
                opcode(record);
                record.strings.push_back(text()); // Method name, then declaring type.
                record.strings.push_back(text());
                argument_types(record);
                break;
            case Kind::EmitInternalMethod:
                opcode(record);
                record.strings.push_back(text());
                if (!code.methods.contains(record.strings.front()))
                    invalid();
                break;
            case Kind::EmitConstructor:
                opcode(record);
                record.strings.push_back(text());
                argument_types(record);
                break;
            case Kind::EmitDouble:
                opcode(record);
                if (*record.opcode != 0x23)
                    invalid();
                record.floating_bits = number<std::uint64_t>();
                break;
            case Kind::EmitFloat:
                opcode(record);
                if (*record.opcode != 0x22)
                    invalid();
                record.floating_bits = number<std::uint32_t>();
                break;
            case Kind::EmitInteger:
                opcode(record);
                record.integers.push_back(number<std::int32_t>());
                break;
            default:
                invalid();
            }
            method->records.push_back(std::move(record));
        }
    }
};
} // namespace

std::optional<YEngineObjectCode> decode_yengine_object_code(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > 64 * 1024 * 1024)
        return {};
    try {
        return ObjectReader(bytes).read();
    } catch (const std::invalid_argument &) {
        return {};
    }
}

std::string yengine_object_cache_key(std::string_view source, const framework::Uuid &asset) {
    constexpr std::string_view alphabet = "0123456789_abcdefghijklmnopqrstuvwxyz@ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto end = source.size();
    while (end && static_cast<unsigned char>(source[end - 1]) <= ' ')
        --end;
    std::size_t begin = 0;
    while (begin < end) {
        if (static_cast<unsigned char>(source[begin]) <= ' ') {
            ++begin;
            continue;
        }
        if (source.substr(begin, 2) != "//")
            break;
        const auto newline = source.find('\n', begin);
        begin = newline == source.npos ? end : newline + 1;
    }
    if (begin >= end)
        return asset.str();
    const auto body = source.substr(begin, end - begin);
    if (body.size() == 43 && body.find_first_not_of(alphabet) == body.npos)
        return std::string(body);
    const auto hash = framework::sha256_hex(source);
    std::string result;
    std::uint32_t bits = 0;
    unsigned count = 0;
    for (std::size_t index = 0; index < hash.size(); index += 2) {
        unsigned byte = 0;
        const auto parsed = std::from_chars(hash.data() + index, hash.data() + index + 2, byte, 16);
        if (parsed.ec != std::errc{})
            return {};
        bits |= byte << count;
        count += 8;
        while (count >= 6) {
            result.push_back(alphabet[bits & 63]);
            bits >>= 6;
            count -= 6;
        }
    }
    if (count)
        result.push_back(alphabet[bits & 63]);
    return result;
}

std::optional<YEngineObjectCode> read_yengine_object_cache(const std::filesystem::path &directory,
                                                           const framework::Uuid &asset, std::string_view source,
                                                           std::string_view source_hash) {
    if (directory.empty() || source_hash.size() != 32)
        return {};
    const auto key = yengine_object_cache_key(source, asset);
    if (key.empty())
        return {};
    const auto filename = key + ".yobj";
    auto path = directory / filename.substr(0, 1) / filename.substr(1);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error)
            return {};
        path = directory / filename;
    }
    if (!std::filesystem::is_regular_file(path, error) || error)
        return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (length <= 0 || length > 64 * 1024 * 1024)
        return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return {};
    auto code = decode_yengine_object_code(bytes);
    if (!code || code->source_hash != source_hash)
        return {};
    return code;
}
} // namespace opensim::script

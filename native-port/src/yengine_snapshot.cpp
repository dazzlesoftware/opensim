// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <bit>
#include <stdexcept>

namespace opensim::script {
namespace {
enum class Tag : std::uint8_t {
    Null = 0, LslFloat = 2, LslInteger = 3, LslKey = 4, LslList = 5,
    LslRotation = 6, LslString = 7, LslVector = 8, Array = 9,
    Double = 10, Float = 11, Integer = 12, String = 13, Reference = 15,
};

using Array = std::shared_ptr<const std::vector<LslValue>>;
using Object = std::variant<std::monostate, LslValue, Array>;

class SnapshotReader {
    std::span<const std::uint8_t> bytes_;
    std::vector<std::optional<Object>> references_;
    std::size_t entries_left_ = 65536;
    std::size_t string_bytes_left_ = 4 * 1024 * 1024;

    [[noreturn]] static void invalid() {
        throw std::invalid_argument("Unsupported YEngine snapshot");
    }

    std::span<const std::uint8_t> take(std::size_t count) {
        if (count > bytes_.size())
            invalid();
        const auto field = bytes_.first(count);
        bytes_ = bytes_.subspan(count);
        return field;
    }

    static void consume(std::size_t& budget, std::size_t count) {
        if (count > budget)
            invalid();
        budget -= count;
    }

    template <class Number> Number number() {
        using Bits = std::conditional_t<sizeof(Number) == 8, std::uint64_t, std::uint32_t>;
        Bits bits = 0;
        unsigned shift = 0;
        for (const auto byte : take(sizeof(Number))) {
            bits |= static_cast<Bits>(byte) << shift;
            shift += 8;
        }
        return std::bit_cast<Number>(bits);
    }

    std::string_view text() {
        std::uint32_t length = 0;
        for (unsigned shift = 0; shift <= 28; shift += 7) {
            const auto byte = take(1).front();
            if (shift == 28 && byte > 7)
                invalid();
            length |= static_cast<std::uint32_t>(byte & 127) << shift;
            if ((byte & 128) == 0) {
                const auto field = take(length);
                return {reinterpret_cast<const char*>(field.data()), field.size()};
            }
        }
        invalid();
    }

    Object read_array(unsigned depth) {
        const auto element_type = text();
        const auto length = number<std::int32_t>();
        if (length < 0 || length > 65536 || static_cast<std::size_t>(length) > bytes_.size())
            invalid();
        consume(entries_left_, static_cast<std::size_t>(length));
        auto array = std::make_shared<std::vector<LslValue>>();
        array->reserve(length);
        for (int i = 0; i < length; ++i) {
            auto element = object(depth + 1);
            if (auto* value = std::get_if<LslValue>(&element))
                array->push_back(std::move(*value));
            else if (std::holds_alternative<std::monostate>(element) && element_type == "st")
                array->emplace_back(std::string{});
            else if (std::holds_alternative<std::monostate>(element) && element_type == "li")
                array->emplace_back(std::make_shared<const LslList>());
            else
                invalid();
        }
        return Array(std::move(array));
    }

    Object object(unsigned depth = 0) {
        if (depth >= 64)
            invalid();
        const auto tag = static_cast<Tag>(take(1).front());
        switch (tag) {
        case Tag::Null:
            return std::monostate{};
        case Tag::LslFloat:
        case Tag::Double:
            return LslValue(number<double>());
        case Tag::Float:
            return LslValue(static_cast<double>(number<float>()));
        case Tag::LslInteger:
        case Tag::Integer:
            return LslValue(number<std::int32_t>());
        case Tag::LslRotation:
            return LslValue(LslRotation{number<double>(), number<double>(), number<double>(), number<double>()});
        case Tag::LslVector:
            return LslValue(framework::Vector3d{number<double>(), number<double>(), number<double>()});
        case Tag::LslKey:
        case Tag::LslString: {
            auto value = object(depth + 1);
            if (std::holds_alternative<std::monostate>(value))
                return LslValue(std::string{});
            const auto* scalar = std::get_if<LslValue>(&value);
            if (!scalar || !std::holds_alternative<std::string>(*scalar))
                invalid();
            return value;
        }
        case Tag::Reference: {
            const auto index = number<std::uint32_t>();
            if (index >= references_.size() || !references_[index])
                invalid();
            if (const auto* value = std::get_if<LslValue>(&*references_[index]))
                if (const auto* string = std::get_if<std::string>(value))
                    consume(string_bytes_left_, string->size());
            return *references_[index];
        }
        case Tag::String:
        case Tag::Array:
        case Tag::LslList: {
            const auto index = references_.size();
            if (index == 65536)
                invalid();
            references_.emplace_back();
            Object value;
            if (tag == Tag::String)
                value = LslValue(std::string(text()));
            else if (tag == Tag::Array)
                value = read_array(depth);
            else {
                auto elements = object(depth + 1);
                const auto* array = std::get_if<Array>(&elements);
                if (!array && !std::holds_alternative<std::monostate>(elements))
                    invalid();
                auto list = std::make_shared<LslList>();
                if (array) {
                    consume(entries_left_, (*array)->size());
                    for (const auto& element : **array)
                        if (const auto* string = std::get_if<std::string>(&element))
                            consume(string_bytes_left_, string->size());
                    list->items = **array;
                }
                value = LslValue(std::shared_ptr<const LslList>(std::move(list)));
            }
            references_[index] = value;
            return value;
        }
        }
        invalid();
    }

    std::vector<LslValue> array() {
        auto value = object();
        if (const auto* array = std::get_if<Array>(&value))
            return **array;
        if (std::holds_alternative<std::monostate>(value))
            return {};
        invalid();
    }

  public:
    explicit SnapshotReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    YEngineSnapshot read() {
        if (take(1).front() != 12)
            invalid();
        take(1); // Reserved byte ignored by the managed reader.
        auto arguments = array();
        const auto initialize_globals = take(1).front();
        if (initialize_globals > 1)
            invalid();
        const auto state = number<std::int32_t>();
        const auto event = number<std::int32_t>();
        if (state < 0 || event < -1 || event >= 42 || (event == -1 && !arguments.empty()))
            invalid();
        YEngineSnapshot snapshot{};
        snapshot.state_index = static_cast<std::size_t>(state);
        snapshot.event_code = event;
        snapshot.initialize_globals = initialize_globals != 0;
        snapshot.event_arguments = std::move(arguments);
        for (auto& globals : snapshot.globals)
            globals = array();
        auto frame = text();
        if (frame == "**newStateCode**") {
            const auto target = number<std::int32_t>();
            if (target < 0 || event != 1)
                invalid();
            snapshot.next_state_index = static_cast<std::size_t>(target);
            frame = text();
        }
        if (!frame.empty() || !bytes_.empty())
            invalid(); // Suspended managed stack frame.
        return snapshot;
    }
};
}

std::optional<YEngineSnapshot> decode_yengine_snapshot(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > 4 * 1024 * 1024)
        return std::nullopt;
    try {
        return SnapshotReader(bytes).read();
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}
}

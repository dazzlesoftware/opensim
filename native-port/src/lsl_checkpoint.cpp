// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace opensim::script {
namespace {
constexpr std::array<std::uint8_t, 4> signature{'L', 'S', 'C', 1};
constexpr std::array<std::uint8_t, 4> listener_signature{'L', 'S', 'L', 1};
constexpr std::uint8_t listener_version = 2;
constexpr std::uint8_t permissions_version = 3;
constexpr std::uint8_t start_parameter_version = 4;
constexpr std::uint8_t sensor_version = 5;
constexpr std::uint8_t regex_listener_version = 6;
constexpr std::uint8_t regex_listener_blob_version = 2;

bool has_regex_listeners(const std::vector<LslListenerCheckpoint>& listeners) {
    return std::ranges::any_of(listeners, [](const auto& listener) { return listener.regex_flags != 0; });
}
constexpr std::size_t maximum_bytes = 4 * 1024 * 1024;
constexpr std::size_t maximum_entries = 65536;
constexpr unsigned maximum_depth = 64;

template <class Archive, class Checkpoint> void checkpoint_fields(Archive& archive, Checkpoint& value) {
    archive.fields(value.program_hash, value.state, value.next_state, value.globals, value.locals,
                   value.queued_events, value.active_event, value.initialized_globals, value.instruction,
                   value.sleep_remaining, value.timer_interval, value.timer_remaining, value.timer_elapsed,
                   value.minimum_event_delay, value.event_delay_remaining, value.running,
                   value.sleep_requested);
}
template <class Archive, class Event> void event_fields(Archive& archive, Event& value) {
    archive.fields(value.name, value.arguments, value.detectors);
}
template <class Archive, class Listener> void listener_fields(Archive& archive, Listener& value) {
    archive.fields(value.sender, value.handle, value.channel, value.name, value.message, value.active);
}
template <class Archive, class Detector> void detector_fields(Archive& archive, Detector& value) {
    archive.fields(value.key, value.touch_face, value.touch_pos, value.touch_normal, value.touch_binormal,
                   value.touch_st, value.touch_uv, value.name, value.owner, value.group, value.position,
                   value.velocity, value.grab, value.rotation, value.type, value.link_number);
}

class Writer {
    std::vector<std::uint8_t> output_{signature.begin(), signature.end()};
    unsigned depth_ = 0;
    bool include_listener_regex_ = false;

    void append(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > maximum_bytes - output_.size())
            throw std::length_error("LSL checkpoint exceeds the transfer limit");
        output_.insert(output_.end(), bytes.begin(), bytes.end());
    }
    void number(std::uint64_t bits, unsigned width) {
        std::array<std::uint8_t, 8> bytes{};
        for (unsigned i = 0; i < width; ++i)
            bytes[i] = static_cast<std::uint8_t>(bits >> (i * 8));
        append(std::span(bytes).first(width));
    }
    void field(bool value) {
        number(value, 1);
    }
    void field(std::int32_t value) {
        number(std::bit_cast<std::uint32_t>(value), 4);
    }
    void field(std::int64_t value) {
        number(std::bit_cast<std::uint64_t>(value), 8);
    }
    void field(std::size_t value) {
        number(value, 8);
    }
    void field(double value) {
        number(std::bit_cast<std::uint64_t>(value), 8);
    }
    void field(const std::string& value) {
        if (value.size() > maximum_bytes)
            throw std::length_error("LSL checkpoint string exceeds the transfer limit");
        number(value.size(), 4);
        append({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
    }
    void field(const framework::Uuid& value) {
        append(value.bytes());
    }
    void field(const framework::Vector3d& value) {
        fields(value.x, value.y, value.z);
    }
    void field(const LslRotation& value) {
        fields(value.x, value.y, value.z, value.w);
    }
    void field(const LslEvent& value) {
        event_fields(*this, value);
    }
    void field(const LslDetector& value) {
        detector_fields(*this, value);
    }
    void field(const LslListenerCheckpoint& value) {
        listener_fields(*this, value);
        if (include_listener_regex_)
            field(value.regex_flags);
    }
    void field(const LslSensorQuery& value) {
        fields(value.name, value.key, value.type, value.range, value.arc, value.interval);
    }
    template <class T> void field(const std::vector<T>& values) {
        if (values.size() > maximum_entries)
            throw std::length_error("LSL checkpoint collection exceeds the transfer limit");
        number(values.size(), 4);
        for (const auto& value : values)
            field(value);
    }
    template <class T> void field(const std::optional<T>& value) {
        field(value.has_value());
        if (value)
            field(*value);
    }
    void field(const LslValue& value) {
        // Tags are fixed by the wire version and independent of host ABI sizes.
        number(value.index(), 1);
        std::visit([&](const auto& item) { field(item); }, value);
    }
    void field(const std::shared_ptr<const LslList>& value) {
        if (!value || depth_ == maximum_depth)
            throw std::invalid_argument("Invalid nested LSL checkpoint list");
        ++depth_;
        field(value->items);
        --depth_;
    }

  public:
    template <class... T> void fields(const T&... values) {
        (field(values), ...);
    }
    std::vector<std::uint8_t> encode(const LslCheckpoint& checkpoint, bool include_extensions) {
        checkpoint_fields(*this, checkpoint);
        const bool has_permissions = !checkpoint.permissions_granter.is_zero() || checkpoint.permissions_mask != 0;
        include_listener_regex_ = include_extensions && has_regex_listeners(checkpoint.listeners);
        if (checkpoint.start_parameter != 0 || !checkpoint.sensors.empty() || include_listener_regex_) {
            output_[3] = include_listener_regex_ ? regex_listener_version
                       : checkpoint.sensors.empty() ? start_parameter_version : sensor_version;
            if (include_extensions) {
                fields(checkpoint.listeners, checkpoint.permissions_granter, checkpoint.permissions_mask);
            } else {
                fields(std::vector<LslListenerCheckpoint>{}, framework::Uuid{}, std::int32_t{0});
            }
            field(checkpoint.start_parameter);
            if (output_[3] >= sensor_version)
                field(checkpoint.sensors);
        } else if (include_extensions && (!checkpoint.listeners.empty() || has_permissions)) {
            output_[3] = has_permissions ? permissions_version : listener_version;
            field(checkpoint.listeners);
            if (has_permissions)
                fields(checkpoint.permissions_granter, checkpoint.permissions_mask);
        }
        return std::move(output_);
    }
    std::vector<std::uint8_t> encode_listeners(const std::vector<LslListenerCheckpoint>& listeners) {
        output_.assign(listener_signature.begin(), listener_signature.end());
        include_listener_regex_ = has_regex_listeners(listeners);
        if (include_listener_regex_)
            output_[3] = regex_listener_blob_version;
        field(listeners);
        return std::move(output_);
    }
};

class Reader {
    std::span<const std::uint8_t> remaining_;
    unsigned depth_ = 0;
    bool include_listener_regex_ = false;

    [[noreturn]] static void invalid() {
        throw std::invalid_argument("Invalid LSL checkpoint");
    }
    std::span<const std::uint8_t> take(std::size_t count) {
        if (count > remaining_.size())
            invalid();
        const auto bytes = remaining_.first(count);
        remaining_ = remaining_.subspan(count);
        return bytes;
    }
    std::uint64_t number(unsigned width) {
        const auto bytes = take(width);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= std::uint64_t(bytes[i]) << (i * 8);
        return value;
    }
    void field(bool& value) {
        const auto bit = number(1);
        if (bit > 1)
            invalid();
        value = bit != 0;
    }
    void field(std::int32_t& value) {
        value = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(number(4)));
    }
    void field(std::int64_t& value) {
        value = std::bit_cast<std::int64_t>(number(8));
    }
    void field(std::size_t& value) {
        const auto size = number(8);
        if (size > std::numeric_limits<std::size_t>::max())
            invalid();
        value = static_cast<std::size_t>(size);
    }
    void field(double& value) {
        value = std::bit_cast<double>(number(8));
    }
    void field(std::string& value) {
        const auto bytes = take(static_cast<std::size_t>(number(4)));
        value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    void field(framework::Uuid& value) {
        value = framework::Uuid::from_bytes(take(16));
    }
    void field(framework::Vector3d& value) {
        fields(value.x, value.y, value.z);
    }
    void field(LslRotation& value) {
        fields(value.x, value.y, value.z, value.w);
    }
    void field(LslEvent& value) {
        event_fields(*this, value);
    }
    void field(LslDetector& value) {
        detector_fields(*this, value);
    }
    void field(LslListenerCheckpoint& value) {
        listener_fields(*this, value);
        if (include_listener_regex_)
            field(value.regex_flags);
    }
    void field(LslSensorQuery& value) {
        fields(value.name, value.key, value.type, value.range, value.arc, value.interval);
        if (value.interval && (!std::isfinite(*value.interval) || *value.interval == 0.))
            invalid();
    }
    template <class T> void field(std::vector<T>& values) {
        const auto count = number(4);
        if (count > maximum_entries || count > remaining_.size())
            invalid();
        for (std::uint64_t i = 0; i < count; ++i)
            field(values.emplace_back());
    }
    template <class T> void field(std::optional<T>& value) {
        bool present;
        field(present);
        if (present)
            field(value.emplace());
    }
    void field(LslValue& value) {
        switch (number(1)) {
        case 0:
            field(value.emplace<std::int32_t>());
            break;
        case 1:
            field(value.emplace<std::string>());
            break;
        case 2:
            field(value.emplace<double>());
            break;
        case 3:
            field(value.emplace<framework::Vector3d>());
            break;
        case 4:
            field(value.emplace<LslRotation>());
            break;
        case 5:
            field(value.emplace<std::shared_ptr<const LslList>>());
            break;
        default:
            invalid();
        }
    }
    void field(std::shared_ptr<const LslList>& value) {
        if (depth_ == maximum_depth)
            invalid();
        ++depth_;
        auto list = std::make_shared<LslList>();
        field(list->items);
        value = std::move(list);
        --depth_;
    }

    void read_listeners(std::vector<LslListenerCheckpoint>& listeners) {
        field(listeners);
        std::set<std::int32_t> handles;
        for (const auto& listener : listeners)
            if (listener.handle <= 0 || !handles.insert(listener.handle).second)
                invalid();
    }

  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : remaining_(bytes) {}
    template <class... T> void fields(T&... values) {
        (field(values), ...);
    }
    LslCheckpoint decode() {
        if (remaining_.size() > maximum_bytes)
            invalid();
        const auto header = take(signature.size());
        if (!std::ranges::equal(header.first(3), std::span(signature).first(3)) ||
            header[3] < signature[3] || header[3] > regex_listener_version)
            invalid();
        LslCheckpoint checkpoint;
        checkpoint_fields(*this, checkpoint);
        include_listener_regex_ = header[3] >= regex_listener_version;
        if (header[3] >= listener_version)
            read_listeners(checkpoint.listeners);
        if (header[3] >= permissions_version)
            fields(checkpoint.permissions_granter, checkpoint.permissions_mask);
        if (header[3] >= start_parameter_version)
            field(checkpoint.start_parameter);
        if (header[3] >= sensor_version)
            field(checkpoint.sensors);
        if (!remaining_.empty())
            invalid();
        return checkpoint;
    }
    std::vector<LslListenerCheckpoint> decode_listeners() {
        if (remaining_.size() > maximum_bytes)
            invalid();
        const auto header = take(listener_signature.size());
        if (!std::ranges::equal(header.first(3), std::span(listener_signature).first(3)) ||
            header[3] < listener_signature[3] || header[3] > regex_listener_blob_version)
            invalid();
        include_listener_regex_ = header[3] >= regex_listener_blob_version;
        std::vector<LslListenerCheckpoint> listeners;
        read_listeners(listeners);
        if (!remaining_.empty())
            invalid();
        return listeners;
    }
};
}

std::vector<std::uint8_t> encode_lsl_checkpoint(const LslCheckpoint& checkpoint, bool include_extensions) {
    return Writer{}.encode(checkpoint, include_extensions);
}
std::vector<std::uint8_t> encode_lsl_listeners(const std::vector<LslListenerCheckpoint>& listeners) {
    return Writer{}.encode_listeners(listeners);
}
std::optional<std::vector<LslListenerCheckpoint>> decode_lsl_listeners(std::span<const std::uint8_t> bytes) {
    try {
        return Reader{bytes}.decode_listeners();
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}
std::optional<LslCheckpoint> decode_lsl_checkpoint(std::span<const std::uint8_t> bytes) {
    try {
        return Reader{bytes}.decode();
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}
}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/uuid_gatherer.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <unordered_set>
namespace opensim::scene {
namespace {
void increment(std::int32_t& value) {
    value = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) + 1u);
}
}
UuidGatherer::UuidGatherer(std::shared_ptr<UuidGathererBackend> b, std::shared_ptr<UuidGathererState> s)
    : backend_(std::move(b)), state_(std::move(s)) {
    if (!state_ || !backend_ || !backend_->get_asset || !backend_->wearable || !backend_->gesture ||
        !backend_->notecard || !backend_->embedded || !backend_->material || !backend_->object ||
        !backend_->error || !backend_->debug)
        throw std::invalid_argument("UuidGatherer requires complete inspection and service dependencies");
}
bool UuidGatherer::skipped(Uuid id) {
    static const std::unordered_set<Uuid, framework::UuidHash> ids{
#include "uuid_gatherer_skip.inc"
    };
    return ids.contains(id);
}
void UuidGatherer::record_failure(Uuid id) {
    if (state_->uncertain.contains(id))
        increment(possible_not_asset_count);
    else
        increment(error_count);
}
bool UuidGatherer::add_gathered(Uuid id, std::int8_t type) {
    if (id.is_zero() || skipped(id))
        return false;
    if (state_->failed.contains(id)) {
        record_failure(id);
        return false;
    }
    if (state_->gathered.contains(id) || std::find(queue_.begin(), queue_.end(), id) != queue_.end())
        return false;
    state_->gathered[id] = type;
    return true;
}
bool UuidGatherer::add_for_inspection(Uuid id) {
    if (id.is_zero() || skipped(id))
        return false;
    if (state_->failed.contains(id)) {
        record_failure(id);
        return false;
    }
    if (state_->gathered.contains(id) || std::find(queue_.begin(), queue_.end(), id) != queue_.end())
        return false;
    queue_.push_back(id);
    return true;
}
void UuidGatherer::add_for_inspection(Uuid id, std::int8_t type) {
    if (id.is_zero())
        return;
    if (state_->failed.contains(id)) {
        record_failure(id);
        return;
    }
    if (state_->gathered.contains(id))
        return;
    try {
        switch (type) {
        case 13:
        case 5:
        case 21:
        case 7:
        case 10:
        case -2:
        case 6:
        case 56:
        case 57:
            add_for_inspection(id);
            break;
        default:
            state_->gathered[id] = type;
            break;
        }
    } catch (...) {
        backend_->error("[UUID GATHERER]: Failed to gather uuids for asset id " + id.str() + ", type " +
                        std::to_string(type));
        throw;
    }
}
bool UuidGatherer::gather_next() {
    if (complete())
        return false;
    auto id = queue_.front();
    queue_.pop_front();
    get_asset_uuids(id);
    return !complete();
}
bool UuidGatherer::gather_all(bool report) {
    if (complete())
        return false;
    if (report)
        verbose_ = false;
    while (gather_next()) {
    }
    if (report && state_->failed.size()) {
        std::string message = "[UUID GATHERER]: UUIDs that are not assets or really missing assets:\n\t";
        bool first = true;
        for (auto id : state_->failed.values()) {
            if (!first)
                message += ',';
            first = false;
            message += id.str();
        }
        backend_->debug(message);
    }
    return true;
}
void UuidGatherer::get_asset_uuids(Uuid id) {
    if (id.is_zero())
        return;
    if (state_->failed.contains(id)) {
        record_failure(id);
        return;
    }
    if (state_->gathered.contains(id))
        return;
    Asset asset;
    try {
        asset = backend_->get_asset(id);
    } catch (const std::exception& e) {
        if (verbose_)
            backend_->error("[UUID GATHERER]: Failed to get asset " + id.str() + " : " + e.what());
        increment(error_count);
        state_->failed.add(id);
        return;
    }
    if (!asset) {
        state_->failed.add(id);
        record_failure(id);
        return;
    }
    increment(asset_get_count);
    state_->uncertain.erase(id);
    if (!asset->data || asset->data->empty()) {
        increment(error_count);
        state_->failed.add(id);
        return;
    }
    auto type = asset->type();
    state_->gathered[id] = type;
    if (asset->data->size() < 36)
        return;
    try {
        switch (type) {
        case 13:
        case 5:
            backend_->wearable(*this, asset);
            break;
        case 21:
            backend_->gesture(*this, asset);
            break;
        case 7:
            backend_->notecard(*this, asset);
            break;
        case 10:
        case 56:
        case 57:
            backend_->embedded(*this, asset);
            break;
        case -2:
            backend_->material(*this, asset);
            break;
        case 6:
            backend_->object(*this, asset);
            break;
        default:
            break;
        }
    } catch (const std::exception& e) {
        if (verbose_)
            backend_->error("[UUID GATHERER]: Failed to gather uuids for asset with id " + id.str() +
                            " type " + std::to_string(type) + ": " + e.what());
        state_->gathered.erase(id);
        increment(error_count);
        state_->failed.add(id);
    }
}
void UuidGatherer::inspect_embedded(std::span<const std::uint8_t> data) {
    std::int64_t index = 8, length = static_cast<std::int64_t>(data.size());
    while (index < length - 28) {
        if (data[static_cast<std::size_t>(index)] == '-') {
            Uuid id;
            auto p = reinterpret_cast<const char*>(data.data() + index - 8);
            if (framework::try_parse_sdk_uuid(std::string_view(p, 36), id)) {
                if (!id.is_zero()) {
                    state_->uncertain.add(id);
                    add_for_inspection(id);
                }
                index += 37;
            } else
                index += 9;
        } else
            ++index;
    }
}
void UuidGatherer::inspect_embedded(std::u16string_view data) {
    if (data.size() < 36)
        return;
    std::int64_t index = 8, length = static_cast<std::int64_t>(data.size());
    while (index < length - 28) {
        if (data[static_cast<std::size_t>(index)] == u'-') {
            std::string candidate;
            for (std::int64_t j = index - 8; j < index + 28; ++j) {
                auto c = data[static_cast<std::size_t>(j)];
                candidate += c < 128 ? static_cast<char>(c) : '?';
            }
            Uuid id;
            if (framework::try_parse_sdk_uuid(candidate, id)) {
                if (!id.is_zero()) {
                    state_->uncertain.add(id);
                    add_for_inspection(id);
                }
                index += 37;
            } else
                index += 9;
        } else
            ++index;
    }
}
}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/get_assets.hpp"
#include "opensim/server/http_range.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/service_protocol.hpp"
#include <algorithm>
#include <condition_variable>
namespace opensim::server {
namespace {
template <class T> T &required(const std::shared_ptr<T> &value) {
    if (!value)
        throw std::invalid_argument("Null asset delivery reference");
    return *value;
}
struct Completion {
    std::mutex mutex;
    std::condition_variable ready;
    std::shared_ptr<framework::AssetBase> asset;
    bool signaled = false, disposed = false;
    void complete(std::shared_ptr<framework::AssetBase> value) {
        {
            std::lock_guard lock(mutex);
            asset = std::move(value);
            if (disposed)
                throw std::runtime_error("Asset completion event is disposed");
            signaled = true;
        }
        ready.notify_all();
    }
    std::shared_ptr<framework::AssetBase> current() {
        std::lock_guard lock(mutex);
        return asset;
    }
    void wait() {
        std::unique_lock lock(mutex);
        ready.wait(lock, [&] { return signaled; });
        disposed = true;
    }
};
const std::unordered_map<std::string, int8_t> types = {
    {"texture_id", 0},   {"sound_id", 1},     {"callcard_id", 2}, {"landmark_id", 3},  {"script_id", 10},
    {"clothing_id", 5},  {"object_id", 6},    {"notecard_id", 7}, {"lsltext_id", 10},  {"lslbyte_id", 11},
    {"txtr_tga_id", 12}, {"bodypart_id", 13}, {"snd_wav_id", 17}, {"img_tga_id", 18},  {"jpeg_id", 19},
    {"animatn_id", 20},  {"gesture_id", 21},  {"mesh_id", 49},    {"settings_id", 56}, {"material_id", 57}};
std::string decode(std::string_view text) {
    auto digit = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    std::string escaped;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && (i + 2 >= text.size() || !digit(text[i + 1]) || !digit(text[i + 2])))
            escaped += "%25";
        else
            escaped += text[i];
    }
    return network::service_url_decode(escaped);
}
}
std::shared_ptr<AssetQueries> asset_query_dictionary(std::string_view target) {
    auto result = std::make_shared<AssetQueries>();
    const auto question = target.find('?');
    if (question == target.npos)
        return result;
    target = target.substr(question + 1);
    target = target.substr(0, target.find('#'));
    if (target.empty())
        return result;
    std::vector<std::pair<std::string, std::string>> values;
    size_t at = 0;
    while (at <= target.size()) {
        auto end = target.find('&', at);
        auto part = target.substr(at, end == target.npos ? end : end - at);
        auto equal = part.find('=');
        if (equal != part.npos) {
            auto name = decode(part.substr(0, equal));
            auto value = decode(part.substr(equal + 1));
            if (!name.empty()) {
                auto old = std::find_if(values.begin(), values.end(), [&](const auto &item) {
                    return framework::ordinal_equals_ignore_case(item.first, name);
                });
                if (old == values.end())
                    values.emplace_back(std::move(name), std::move(value));
                else
                    old->second += "," + value;
            }
        }
        if (end == target.npos)
            break;
        at = end + 1;
    }
    for (auto &[name, value] : values)
        (*result)[name] = std::move(value);
    return result;
}
std::shared_ptr<AssetQueries> AssetHttpRequest::queries() {
    std::lock_guard lock(query_mutex_);
    if (!queries_)
        queries_ = asset_query_dictionary(native.raw_target.empty() ? native.path : native.raw_target);
    return queries_;
}
std::optional<std::string> AssetHttpRequest::range() {
    for (auto &[name, value] : native.headers)
        if (framework::invariant_equals_ignore_case(name, "range"))
            return value;
    return {};
}
void GetAssetsHandler::handle(std::shared_ptr<AssetHttpRequest> request,
                              std::shared_ptr<HttpResponseState> response,
                              const std::optional<std::string> &service_url) {
    [[maybe_unused]] const auto lifetime = weak_from_this().lock();
    required(response).content_type("text/plain");
    if (!service_) {
        response->status(503);
        response->connection(HttpConnectionType::Close);
        return;
    }
    response->status(400);
    auto queries = required(request).queries();
    if (required(queries).empty())
        return;
    int8_t type = -1;
    std::optional<std::string> asset_text = std::string{};
    for (auto &[name, value] : *queries) {
        auto found = types.find(name);
        type = found == types.end() ? int8_t(0) : found->second;
        if (found != types.end()) {
            asset_text = value;
            break;
        }
    }
    if (type == -1) {
        if (warn_)
            warn_("[GETASSET]: Unknown type");
        response->status(404);
        return;
    }
    if (!asset_text || asset_text->empty())
        return;
    framework::Uuid id;
    if (!framework::try_parse_sdk_uuid(*asset_text, id))
        return;
    auto done = std::make_shared<Completion>();
    service_->get(id.str(), service_url, false, [done](auto asset) { done->complete(std::move(asset)); });
    done->wait();
    if (!done->current()) {
        response->status(404);
        return;
    }
    auto bytes = required(done->current()).data;
    int32_t length = static_cast<int32_t>(required(bytes).size());
    if (length == 0) {
        if (warn_)
            warn_("[GETASSET]: asset with empty data: " + *asset_text + " type " +
                  std::to_string(required(done->current()).type()));
        response->status(404);
        return;
    }
    if (required(done->current()).type() != type && warn_)
        warn_("[GETASSET]: asset with wrong type: " + *asset_text + " " +
              std::to_string(required(done->current()).type()) + " != " + std::to_string(type));
    int32_t start = 0, end = 0;
    if (try_parse_http_range(request->range(), start, end)) {
        if (start >= length)
            response->status(200);
        else {
            if (end == -1)
                end = length - 1;
            else
                end = std::clamp(end, int32_t(0), length - 1);
            start = std::clamp(start, int32_t(0), end);
            length = end - start + 1;
            response->add_header("Content-Range",
                                 "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" +
                                     std::to_string(required(required(done->current()).data).size()));
            response->status(206);
            response->raw_start(start);
        }
    } else
        response->status(200);
    response->content_type(required(required(done->current()).metadata()).content_type());
    response->raw_buffer(required(done->current()).data);
    response->raw_length(length);
    response->priority(type == 49 || type == 0 ? 2 : 1);
}
}

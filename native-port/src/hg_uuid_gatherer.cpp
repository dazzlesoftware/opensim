// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/hg_uuid_gatherer.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::scene {
struct HGUuidGatherer::Retrieval {
    ForeignGet foreign;
    std::optional<std::string> url;
    std::function<int()> level;
    std::function<void(const std::string &)> debug;
    Asset fetch(framework::Uuid id) {
        auto text = id.str();
        auto requested_url = url;
        auto asset = foreign(text, requested_url, true);
        if (level() > 0) {
            if (!asset)
                debug("[HGUUIDGatherer]: Failed to fetch asset " + text + " from " + url.value_or(""));
            else
                debug("[HGUUIDGatherer]: Copied asset " + text + " from " + url.value_or("") +
                      " to local asset server");
        }
        return asset;
    }
    bool white() const {
        if (!url)
            return true;
        auto text = framework::utf16_replacing(*url);
        return std::all_of(text.begin(), text.end(), framework::unicode_whitespace);
    }
};
std::shared_ptr<UuidGathererBackend> HGUuidGatherer::decorate(std::shared_ptr<UuidGathererBackend> local,
                                                              const std::shared_ptr<Retrieval> &retrieval) {
    if (!local || !local->get_asset || !retrieval->foreign || !retrieval->level || !retrieval->debug)
        throw std::invalid_argument("Incomplete Hypergrid gatherer dependencies");
    auto result = std::make_shared<UuidGathererBackend>(*local);
    auto get = local->get_asset;
    result->get_asset = [get = std::move(get), retrieval](framework::Uuid id) {
        if (retrieval->white())
            return get(id);
        return retrieval->fetch(id);
    };
    return result;
}
HGUuidGatherer::HGUuidGatherer(std::shared_ptr<UuidGathererBackend> local, ForeignGet foreign,
                               std::optional<std::string> url, std::function<int()> level,
                               std::function<void(const std::string &)> debug,
                               std::shared_ptr<UuidGathererState> state)
    : retrieval_(std::make_shared<Retrieval>(
          Retrieval{std::move(foreign), std::move(url), std::move(level), std::move(debug)})),
      core_(decorate(std::move(local), retrieval_), std::move(state)) {}
HGUuidGatherer::HGUuidGatherer(std::shared_ptr<services::AssetService> service,
                               std::optional<std::string> url, std::function<int()> level,
                               std::function<void(const std::string &)> error,
                               std::function<void(const std::string &)> debug,
                               std::shared_ptr<UuidGathererState> state)
    : HGUuidGatherer(
          native_uuid_gatherer_backend(service, std::move(error), debug),
          [service](const auto &id, const auto &foreign, bool local) {
              return service->get_foreign(id, foreign, local);
          },
          std::move(url), std::move(level), debug, std::move(state)) {}
HGUuidGatherer::Asset HGUuidGatherer::fetch_asset(framework::Uuid id) { return retrieval_->fetch(id); }
void HGUuidGatherer::asset_server_url(std::optional<std::string> value) {
    retrieval_->url = std::move(value);
}
std::optional<std::string> HGUuidGatherer::asset_server_url() const { return retrieval_->url; }
}

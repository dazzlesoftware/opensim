// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_cache.hpp"
#include "opensim/services/inventory_caps.hpp"
namespace opensim::services {
class BakedTextureUpload;
struct BakedTextureCapsBackend {
    std::shared_ptr<framework::IAssetCache> cache;
    std::function<framework::Uuid()> new_id;
    std::function<void(std::shared_ptr<BakedTextureUpload>)> register_upload;
    std::function<void(const std::string&)> remove_upload;
    // Start, Stop and Dispose are separate: their source exception boundaries differ.
    std::function<void(const std::string&, int)> timer;
    std::function<void(const std::string&)> dispose_timer;
    std::function<void(const std::string&)> log;
};
class BakedTextureUpload final : public server::CapabilitySimpleHandler {
public:
    BakedTextureUpload(std::string path, framework::Uuid agent, std::string remote_ip,
                       BakedTextureCapsBackend);
    std::optional<std::string> path() const override {
        return path_;
    }
    network::HttpResponse handle(const network::HttpRequest&) override;
    void expire();

private:
    std::string path_;
    framework::Uuid agent_;
    std::string remote_ip_;
    BakedTextureCapsBackend backend_;
};
// UploadBakedTexture accepts any POST body, including empty/malformed LLSD.
class BakedTextureCaps final : public server::CapabilitySimpleHandler {
public:
    BakedTextureCaps(std::string path, framework::Uuid agent, std::string origin, BakedTextureCapsBackend);
    std::optional<std::string> path() const override {
        return path_;
    }
    network::HttpResponse handle(const network::HttpRequest&) override;

private:
    std::string path_;
    framework::Uuid agent_;
    std::string origin_;
    BakedTextureCapsBackend backend_;
};
}

// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/profile_requests.hpp"

namespace opensim::scene {
struct NativeProfileContextServices {
    std::shared_ptr<ProfileUserDirectory> users;
    std::shared_ptr<ProfileHomeGrid> grid;
    std::shared_ptr<ProfileRequestGridService> regions;
    std::shared_ptr<ProfileAccountService> accounts;
    std::shared_ptr<ProfilePresenceService> presence_service;
    std::shared_ptr<ProfileFriends> friends;
    std::shared_ptr<ProfileRequestGroups> groups;
    std::function<std::shared_ptr<ProfileRequestPresence>(framework::Uuid)> presence;
    std::function<std::shared_ptr<ProfileUserAgent>(const ProfileText&)> user_agent;
    std::function<void(framework::Uuid, const ProfileText&, bool)> foreign_image;
    std::function<bool(std::shared_ptr<framework::UserProfileProperties>&, const ProfileText&)>
        legacy_profile;
    std::function<void(std::function<void()>)> schedule;
    std::function<void(std::u16string_view)> log;
    framework::Uuid scope;
    ProfileText profile_uri, system_born;
    bool allow_web_urls = true;
};

class NativeProfileRequestContext final : public ProfileRequestContext {
  public:
    explicit NativeProfileRequestContext(NativeProfileContextServices services)
        : services_(std::move(services)) {
        if (!services_.users || !services_.grid || !services_.regions || !services_.accounts ||
            !services_.schedule)
            throw std::invalid_argument("Missing required advanced profile services");
    }
    std::shared_ptr<ProfileUserDirectory> users() override {
        return services_.users;
    }
    std::shared_ptr<ProfileHomeGrid> grid() override {
        return services_.grid;
    }
    ProfileText local_profile_uri() override {
        return services_.profile_uri;
    }
    std::shared_ptr<ProfileRequestPresence> presence(framework::Uuid id) override {
        return services_.presence ? services_.presence(id) : nullptr;
    }
    void info(std::u16string_view message) override {
        if (services_.log)
            services_.log(message);
    }
    void get_foreign_image(framework::Uuid image, const ProfileText& uri, bool local) override {
        if (services_.foreign_image)
            services_.foreign_image(image, uri, local);
    }
    std::shared_ptr<ProfileRequestGridService> grid_service() override {
        return services_.regions;
    }
    framework::Uuid region_scope() override {
        return services_.scope;
    }
    void warn_pick_parcel(float x, float y, const ProfileText& scene) override {
        info(u"[PROFILES] No parcel at " + framework::utf16(std::to_string(x)) + u", " +
             framework::utf16(std::to_string(y)) + u" in " + scene.value_or(u""));
    }
    bool allow_web_urls() override {
        return services_.allow_web_urls;
    }
    ProfileText system_agent_born() override {
        return services_.system_born;
    }
    std::shared_ptr<ProfileAccountService> account_service() override {
        return services_.accounts;
    }
    std::shared_ptr<ProfileUserAgent> user_agent(const ProfileText& uri) override {
        return services_.user_agent ? services_.user_agent(uri) : nullptr;
    }
    std::shared_ptr<ProfileFriends> friends() override {
        return services_.friends;
    }
    std::shared_ptr<ProfilePresenceService> presence_service() override {
        return services_.presence_service;
    }
    std::shared_ptr<ProfileRequestGroups> groups() override {
        return services_.groups;
    }
    bool legacy_profile(std::shared_ptr<framework::UserProfileProperties>& profile,
                        const ProfileText& uri) override {
        return services_.legacy_profile && services_.legacy_profile(profile, uri);
    }
    void debug(const ProfileText& message, const std::optional<std::u16string>& error) override {
        info(message.value_or(u"") + error.value_or(u""));
    }
    void process_error(const std::exception& error) override {
        info(framework::utf16_replacing(error.what()));
    }
    void schedule(std::function<void()> task) override {
        services_.schedule(std::move(task));
    }

  private:
    NativeProfileContextServices services_;
};
}

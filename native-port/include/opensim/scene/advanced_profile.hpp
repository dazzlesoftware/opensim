// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/basic_profile.hpp"
#include "opensim/framework/http_uri.hpp"
namespace opensim::scene {
class AdvancedProfileLifecycle;
struct ProfileGridInfo {
    virtual ~ProfileGridInfo() = default;
};
struct ProfileGroups {
    virtual ~ProfileGroups() = default;
};
struct ProfileUsers {
    virtual ~ProfileUsers() = default;
};
struct AdvancedProfileClient {
    virtual ~AdvancedProfileClient() = default;
};
class AdvancedProfileRegion {
public:
    virtual ~AdvancedProfileRegion() = default;
    virtual ProfileText name() = 0;
    virtual std::shared_ptr<ProfileGridInfo> grid_info() = 0;
    virtual void register_profile(std::shared_ptr<AdvancedProfileLifecycle>) = 0;
    virtual std::shared_ptr<ProfileGroups> groups_module() = 0;
    virtual std::shared_ptr<ProfileUsers> users_module() = 0;
    virtual void subscribe_new_client(std::function<void(std::shared_ptr<AdvancedProfileClient>)>) = 0;
    virtual void subscribe_client_closed(
        std::function<void(framework::Uuid, std::shared_ptr<AdvancedProfileRegion>)>) = 0;
};
struct ProfileUriResult {
    bool valid = false, resolved = false;
    ProfileText uri;
};
enum class AdvancedProfileLog { Debug, Error };
struct ProfileStatusCommand {
    std::u16string module;
    bool shared;
    std::u16string command, short_help, long_help;
    std::function<void()> handler;
};
class AdvancedProfileEnvironment {
public:
    virtual ~AdvancedProfileEnvironment() = default;
    virtual ProfileUriResult resolve_uri(const ProfileText&);
    virtual void log(AdvancedProfileLog, std::u16string_view) = 0;
    virtual void add_status_command(ProfileStatusCommand) = 0;
    virtual void output(const ProfileText&) = 0;
};
// Shared lifecycle for the full request controller. Abstract callbacks deliberately
// require real client handlers/cache owners before this module can be installed.
class AdvancedProfileLifecycle : public std::enable_shared_from_this<AdvancedProfileLifecycle> {
public:
    explicit AdvancedProfileLifecycle(std::shared_ptr<AdvancedProfileEnvironment>);
    virtual ~AdvancedProfileLifecycle() = default;
    static constexpr std::u16string_view name = u"UserProfileModule";
    ProfileText profile_server_uri;
    bool enabled() const {
        return enabled_;
    }
    bool allow_web_urls() const {
        return allow_web_urls_;
    }
    std::shared_ptr<framework::NiniConfigSourceBase> config() const {
        return config_;
    }
    void config(std::shared_ptr<framework::NiniConfigSourceBase> value) {
        config_ = std::move(value);
    }
    std::optional<std::u16string> replaceable_interface() const {
        return {};
    }
    std::shared_ptr<AdvancedProfileRegion> scene() const {
        return scene_;
    }
    std::shared_ptr<ProfileGridInfo> grid_info() const {
        return grid_;
    }
    std::shared_ptr<ProfileGroups> groups_module() const {
        return groups_;
    }
    std::shared_ptr<ProfileUsers> users_module() const {
        return users_;
    }
    void initialise(std::shared_ptr<framework::NiniConfigSourceBase>);
    void add_region(std::shared_ptr<AdvancedProfileRegion>);
    void region_loaded(std::shared_ptr<AdvancedProfileRegion>);
    void remove_region(std::shared_ptr<AdvancedProfileRegion>);
    void close();
    void show_status();

protected:
    virtual void new_client(std::shared_ptr<AdvancedProfileClient>) = 0;
    virtual void client_closed(framework::Uuid, std::shared_ptr<AdvancedProfileRegion>) = 0;
    virtual void clear_profiles() = 0;
    virtual void clear_classifieds() = 0;
    virtual void clear_classified_interest() = 0;
    virtual std::int32_t local_queue_count() const = 0;
    virtual std::int32_t hg_queue_count() const = 0;

private:
    std::shared_ptr<AdvancedProfileEnvironment> environment_;
    std::shared_ptr<framework::NiniConfigSourceBase> config_;
    std::shared_ptr<AdvancedProfileRegion> scene_;
    std::shared_ptr<ProfileGridInfo> grid_;
    std::shared_ptr<ProfileGroups> groups_;
    std::shared_ptr<ProfileUsers> users_;
    bool enabled_ = false, allow_web_urls_ = true;
};
}

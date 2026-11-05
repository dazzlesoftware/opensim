// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/advanced_profile.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::scene {
namespace {
template <class T> T& required(const std::shared_ptr<T>& p) {
    if (!p)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *p;
}
}
ProfileUriResult AdvancedProfileEnvironment::resolve_uri(const ProfileText& text) {
    framework::HttpUri uri(text ? std::optional<std::string>(framework::utf8(*text)) : std::nullopt, true);
    return {uri.valid(), uri.resolved(), framework::utf16_replacing(uri.uri)};
}
AdvancedProfileLifecycle::AdvancedProfileLifecycle(std::shared_ptr<AdvancedProfileEnvironment> environment)
    : environment_(std::move(environment)) {
}
void AdvancedProfileLifecycle::initialise(std::shared_ptr<framework::NiniConfigSourceBase> config) {
    config_ = std::move(config);
    auto section = required(config_).configs()->at(framework::nini_writer_text(u"UserProfiles"));
    if (!section) {
        enabled_ = false;
        return;
    }
    auto uri = section->get_string(framework::nini_writer_text(u"ProfileServiceURL"),
                                   framework::nini_writer_text(u""));
    profile_server_uri = uri ? ProfileText(*uri) : ProfileText{};
    if (!profile_server_uri || profile_server_uri->empty()) {
        enabled_ = false;
        return;
    }
    auto parsed = required(environment_).resolve_uri(profile_server_uri);
    if (!parsed.resolved) {
        required(environment_)
            .log(AdvancedProfileLog::Error, parsed.valid
                                                ? u"[UserProfileModule: Could not resolve ProfileServiceURL"
                                                : u"[UserProfileModule: ProfileServiceURL is a invalid host");
        throw std::runtime_error("UserProfileModule init error");
    }
    profile_server_uri = std::move(parsed.uri);
    allow_web_urls_ =
        section->get_boolean(framework::nini_writer_text(u"AllowUserProfileWebURLs"), allow_web_urls_);
    required(environment_).log(AdvancedProfileLog::Debug, u"[UserProfileModule]: Full Profiles Enabled");
    auto self = shared_from_this();
    required(environment_)
        .add_status_command({u"Debug", false, u"profiles status", u"profiles status",
                             u"Show user profile Queues count", [self] { self->show_status(); }});
    enabled_ = true;
}
void AdvancedProfileLifecycle::add_region(std::shared_ptr<AdvancedProfileRegion> scene) {
    if (!enabled_)
        return;
    scene_ = std::move(scene);
    if (!grid_)
        grid_ = required(scene_).grid_info();
    required(scene_).register_profile(shared_from_this());
}
void AdvancedProfileLifecycle::region_loaded(std::shared_ptr<AdvancedProfileRegion>) {
    if (!enabled_)
        return;
    groups_ = required(scene_).groups_module();
    users_ = required(scene_).users_module();
    if (!users_) {
        required(environment_)
            .log(AdvancedProfileLog::Error,
                 u"[UserProfileModule]: UserManagementModule not loaded. Profiles Disabled");
        enabled_ = false;
        return;
    }
    auto self = shared_from_this();
    required(scene_).subscribe_new_client([self](auto client) { self->new_client(std::move(client)); });
    required(scene_).subscribe_client_closed(
        [self](auto agent, auto region) { self->client_closed(agent, std::move(region)); });
}
void AdvancedProfileLifecycle::remove_region(std::shared_ptr<AdvancedProfileRegion>) {
    if (!enabled_)
        return;
    clear_profiles();
    clear_classifieds();
    clear_classified_interest();
}
void AdvancedProfileLifecycle::close() {
    grid_.reset();
}
void AdvancedProfileLifecycle::show_status() {
    auto region_name = required(scene_).name();
    auto local = local_queue_count();
    auto hg = hg_queue_count();
    required(environment_)
        .output(u"Profile requests in '" + region_name.value_or(u"") + u"' Local: " +
                framework::utf16(std::to_string(local)) + u" HG: " + framework::utf16(std::to_string(hg)));
}
}

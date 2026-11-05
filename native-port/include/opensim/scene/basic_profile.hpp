// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/nini_config.hpp"
#include "opensim/framework/uuid.hpp"
#include <mutex>
namespace opensim::scene {
using ProfileText = std::optional<std::u16string>;
struct BasicProfileAccount {
    ProfileText first, last;
    std::int32_t created = 0;
};
struct AvatarProfileProperties {
    framework::Uuid avatar, first_life_image, image, partner;
    ProfileText about, born, first_life_about, url;
    std::shared_ptr<std::vector<std::uint8_t>> membership;
    std::uint32_t flags = 0;
};
struct AvatarProfileInterests {
    framework::Uuid avatar;
    std::uint32_t wants = 0, skills = 0;
    ProfileText want_text, skill_text, languages;
};
class BasicProfileModule;
class BasicProfileClient;
class ProfileModule {
public:
    virtual ~ProfileModule() = default;
    virtual void request_properties(std::shared_ptr<BasicProfileClient>, framework::Uuid) = 0;
};
class ProfileSceneBase {
public:
    virtual ~ProfileSceneBase() = default;
};
class BasicProfileAccounts {
public:
    virtual ~BasicProfileAccounts() = default;
    virtual std::shared_ptr<BasicProfileAccount> get(framework::Uuid scope, framework::Uuid avatar) = 0;
};
class BasicProfileScene : public ProfileSceneBase {
public:
    virtual std::shared_ptr<BasicProfileAccounts> accounts() = 0;
    virtual framework::Uuid scope() = 0;
    virtual void on_new_client(std::function<void(std::shared_ptr<BasicProfileClient>)>) = 0;
    virtual void register_profile(std::shared_ptr<ProfileModule>) = 0;
};
class BasicProfileClient {
public:
    virtual ~BasicProfileClient() = default;
    virtual std::shared_ptr<ProfileSceneBase> scene() = 0;
    virtual void
        on_properties_request(std::function<void(std::shared_ptr<BasicProfileClient>, framework::Uuid)>) = 0;
    virtual void send_properties(const AvatarProfileProperties&) = 0;
    virtual void send_interests(const AvatarProfileInterests&) = 0;
};
// CLR instances are heap references. Event handlers retain this instance after
// RemoveRegion/Close as in the source; the host owns graph collection at teardown.
class BasicProfileModule : public ProfileModule, public std::enable_shared_from_this<BasicProfileModule> {
public:
    using Logger = std::function<void(std::u16string_view)>;
    explicit BasicProfileModule(Logger logger = {}) : logger_(std::move(logger)) {}
    static constexpr std::u16string_view name = u"BasicProfileModule";
    void initialise(std::shared_ptr<framework::NiniConfigSourceBase>);
    void add_region(std::shared_ptr<BasicProfileScene>);
    void remove_region(std::shared_ptr<BasicProfileScene>);
    void region_loaded(std::shared_ptr<BasicProfileScene>) const {}
    void close() const {}
    void post_initialise() const {}
    void request_properties(std::shared_ptr<BasicProfileClient>, framework::Uuid) override;

private:
    friend struct BasicProfileProbe;
    std::shared_ptr<BasicProfileScene> first_scene() const;
    void new_client(std::shared_ptr<BasicProfileClient>);
    mutable std::recursive_mutex mutex_;
    std::vector<std::shared_ptr<BasicProfileScene>> scenes_;
    bool enabled_ = false;
    Logger logger_;
};
// Actual bundled Utils.StringToBytes conversion, including zero tails and its
// distinct treatment of malformed UTF-16. It is not Encoding.UTF8.GetBytes.
std::vector<std::uint8_t> profile_string_bytes(std::u16string_view);
std::vector<std::uint8_t> profile_string_bytes(std::u16string_view, std::size_t maximum_length);
}

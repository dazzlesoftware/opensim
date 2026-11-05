// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/basic_profile.hpp"
#include <algorithm>
#include <chrono>
namespace opensim::scene {
namespace {
template <class T> T& required(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *value;
}
}
std::vector<std::uint8_t> profile_string_bytes(std::u16string_view text) {
    if (text.empty())
        return {};
    char16_t last = 0;
    std::ptrdiff_t count = 0;
    std::size_t source = 0;
    while (source < text.size()) {
        auto c = text[source++];
        if (c <= 0x7f) {
            last = c;
            ++count;
        } else if (c < 0x800) {
            last = c;
            count += 2;
        } else if (c >= 0xd800 && c < 0xe000) {
            if (c < 0xdc00) {
                if (source == text.size())
                    break;
                c = text[source++];
                if (c >= 0xdc00 && c <= 0xdfff) {
                    last = c;
                    count += 4;
                }
            }
        } else {
            last = c;
            count += 3;
        }
    }
    if (source > 0 && last == 0) {
        --count;
        --source;
    }
    if (count == 0)
        return {};
    if (count < 0)
        throw std::out_of_range("Index was outside the bounds of the array.");
    std::vector<std::uint8_t> out(static_cast<std::size_t>(count) + 1);
    std::size_t i = 0, at = 0;
    auto byte = [&](std::uint32_t b) { out[at++] = static_cast<std::uint8_t>(b); };
    while (i < source && at < static_cast<std::size_t>(count)) {
        std::uint32_t c = text[i++];
        if (c == 0)
            break;
        if (c <= 0x7f)
            byte(c);
        else if (c < 0x800) {
            if (at + 2 > static_cast<std::size_t>(count))
                break;
            byte(0xc0 | (c >> 6));
            byte(0x80 | (c & 63));
        } else if (c >= 0xd800 && c < 0xe000) {
            if (c < 0xdc00) {
                if (i >= source || at + 4 > static_cast<std::size_t>(count))
                    break;
                auto low = text[i++];
                if (low >= 0xdc00 && low <= 0xdfff) {
                    c = (c << 10) + low - 56613888;
                    byte(0xf0 | (c >> 18));
                    byte(0x80 | ((c >> 12) & 63));
                    byte(0x80 | ((c >> 6) & 63));
                    byte(0x80 | (c & 63));
                }
            }
        } else {
            if (at + 3 > static_cast<std::size_t>(count))
                break;
            byte(0xe0 | (c >> 12));
            byte(0x80 | ((c >> 6) & 63));
            byte(0x80 | (c & 63));
        }
    }
    return out;
}
std::vector<std::uint8_t> profile_string_bytes(std::u16string_view text, std::size_t maximum_length) {
    if (maximum_length <= 1)
        return {};
    std::vector<std::uint8_t> out;
    const auto limit = maximum_length - 1;
    for (std::size_t i = 0; i < text.size() && out.size() < limit;) {
        std::uint32_t c = text[i++];
        if (!c)
            break;
        if (c >= 0xd800 && c < 0xe000) {
            if (c >= 0xdc00)
                continue;
            if (i == text.size())
                break;
            auto low = text[i++];
            if (low < 0xdc00 || low > 0xdfff)
                continue;
            c = (c << 10) + low - 56613888;
        }
        std::size_t width = c <= 0x7f ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (width > limit - out.size())
            break;
        auto byte = [&](std::uint32_t b) { out.push_back(static_cast<std::uint8_t>(b)); };
        if (width == 1)
            byte(c);
        else if (width == 2) {
            byte(0xc0 | (c >> 6));
            byte(0x80 | (c & 63));
        } else if (width == 3) {
            byte(0xe0 | (c >> 12));
            byte(0x80 | ((c >> 6) & 63));
            byte(0x80 | (c & 63));
        } else {
            byte(0xf0 | (c >> 18));
            byte(0x80 | ((c >> 12) & 63));
            byte(0x80 | ((c >> 6) & 63));
            byte(0x80 | (c & 63));
        }
    }
    if (!out.empty())
        out.push_back(0);
    return out;
}
void BasicProfileModule::initialise(std::shared_ptr<framework::NiniConfigSourceBase> config) {
    if (required(config).configs()->at(framework::nini_writer_text(u"UserProfiles")))
        return;
    if (logger_)
        logger_(u"[PROFILE MODULE]: Basic Profile Module enabled");
    enabled_ = true;
}
void BasicProfileModule::add_region(std::shared_ptr<BasicProfileScene> scene) {
    if (!enabled_)
        return;
    std::lock_guard lock(mutex_);
    if (std::find(scenes_.begin(), scenes_.end(), scene) == scenes_.end()) {
        scenes_.push_back(scene);
        auto self = shared_from_this();
        required(scene).on_new_client([self](auto client) { self->new_client(std::move(client)); });
        required(scene).register_profile(std::move(self));
    }
}
void BasicProfileModule::remove_region(std::shared_ptr<BasicProfileScene> scene) {
    if (!enabled_)
        return;
    std::lock_guard lock(mutex_);
    auto i = std::find(scenes_.begin(), scenes_.end(), scene);
    if (i != scenes_.end())
        scenes_.erase(i);
}
std::shared_ptr<BasicProfileScene> BasicProfileModule::first_scene() const {
    std::lock_guard lock(mutex_);
    if (scenes_.empty())
        throw std::out_of_range(
            "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
    return scenes_[0];
}
void BasicProfileModule::new_client(std::shared_ptr<BasicProfileClient> client) {
    auto self = shared_from_this();
    required(client).on_properties_request(
        [self](auto sender, auto avatar) { self->request_properties(std::move(sender), avatar); });
}
void BasicProfileModule::request_properties(std::shared_ptr<BasicProfileClient> client,
                                            framework::Uuid avatar) {
    if (!std::dynamic_pointer_cast<BasicProfileScene>(required(client).scene()))
        return;
    auto accounts = required(first_scene()).accounts();
    auto scope = required(first_scene()).scope();
    auto account = required(accounts).get(scope, avatar);
    std::u16string profile_name = u"Avatar";
    std::int32_t created = 0;
    if (account) {
        profile_name = account->first.value_or(u"") + u" " + account->last.value_or(u"");
        created = account->created;
    }
    auto membership = std::make_shared<std::vector<std::uint8_t>>(profile_string_bytes(profile_name));
    auto date = std::chrono::year_month_day(
        std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds(std::chrono::seconds(created))));
    auto text = std::to_string(static_cast<unsigned>(date.month())) + "/" +
                std::to_string(static_cast<unsigned>(date.day())) + "/" +
                std::to_string(static_cast<int>(date.year()));
    std::u16string born(text.begin(), text.end());
    AvatarProfileProperties props;
    props.avatar = avatar;
    props.about = u"";
    props.born = std::move(born);
    props.membership = std::move(membership);
    props.first_life_about = u"";
    props.url = u"No profile data";
    required(client).send_properties(props);
    AvatarProfileInterests interests;
    interests.avatar = avatar;
    interests.want_text = u"";
    interests.skill_text = u"";
    interests.languages = u"";
    required(client).send_interests(interests);
}
}

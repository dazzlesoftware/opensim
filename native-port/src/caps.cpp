// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/caps.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace opensim::services {
namespace {
const std::string& key(const Caps::Name& value) {
    if (!value)
        throw std::invalid_argument("Null capability name");
    return *value;
}
template <class T> T& require(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::invalid_argument("Null capability dependency");
    return *value;
}
bool requested(const Caps::Requested* names, const std::string& name) {
    if (!names)
        throw std::invalid_argument("Null requested capabilities");
    return std::find(names->begin(), names->end(), Caps::Name(name)) != names->end();
}
class Activation : public CapabilityActivation {
    std::mutex mutex_;
    std::condition_variable changed_;
    bool signaled_ = false, disposed_ = false;
    void check() {
        if (disposed_)
            throw std::runtime_error("Disposed capability activation");
    }

  public:
    void reset() override {
        std::lock_guard lock(mutex_);
        check();
        signaled_ = false;
    }
    void set() override {
        {
            std::lock_guard lock(mutex_);
            check();
            signaled_ = true;
        }
        changed_.notify_all();
    }
    bool wait(int ms) override {
        std::unique_lock lock(mutex_);
        check();
        bool ready =
            changed_.wait_for(lock, std::chrono::milliseconds(ms), [&] { return signaled_ || disposed_; });
        check();
        return ready;
    }
    void dispose() override {
        {
            std::lock_guard lock(mutex_);
            disposed_ = true;
        }
        changed_.notify_all();
    }
};
}
Caps::Caps(std::shared_ptr<CapabilityRuntimeListener> listener, Name host, std::uint32_t port, Name path,
           framework::Uuid agent, Name region, MainServer main, std::shared_ptr<CapabilityActivation> active,
           std::function<void(const std::string&)> error)
    : listener_(std::move(listener)), host_(host), path_(std::move(path)), region_(std::move(region)),
      port_(port), agent_(agent), main_(std::move(main)),
      activation_(active ? std::move(active) : std::make_shared<Activation>()), error_(std::move(error)) {
    if (listener_ && listener_->use_ssl()) {
        port_ = listener_->ssl_port();
        host = listener_->ssl_common_name();
        port = listener_->ssl_port();
    }
    handlers_ = std::make_unique<server::CapsHandlers>(listener_, host, port);
    activation_->reset();
    if (require(main_()).use_ssl()) {
        auto common = require(main_()).ssl_common_name();
        auto ssl_port = require(main_()).ssl_port();
        base_caps_url_ = "https://" + common.value_or("") + ":" + std::to_string(ssl_port);
    } else if (!main_())
        base_caps_url_ = "http://" + host_.value_or("") + ":0";
    else
        base_caps_url_ = "http://" + host_.value_or("") + ":" + std::to_string(require(main_()).port());
}
bool Caps::ssl_caps() const {
    return require(listener_).use_ssl();
}
Caps::Name Caps::ssl_common_name() const {
    return require(listener_).ssl_common_name();
}
void Caps::register_handler(const Name& name, std::shared_ptr<server::CapabilityStreamHandler> handler) {
    handlers_->set(name, std::move(handler));
}
void Caps::register_simple(const Name& name, std::shared_ptr<server::CapabilitySimpleHandler> handler,
                           bool add) {
    handlers_->add_simple(name, std::move(handler), add);
}
void Caps::register_poll(const Name& name, std::shared_ptr<CapabilityPollHandler> handler) {
    const auto& k = key(name);
    bool added;
    {
        std::lock_guard lock(poll_mutex_);
        added = !polls_.contains(k);
        if (added)
            polls_[k] = handler;
    }
    if (!added) {
        auto url = require(handler).url;
        if (error_)
            error_("[CAPS]: Handler with name " + k + " already registered (ulr " + url.value_or("") +
                   ", agent " + agent_.str() + ", region " + region_.value_or(""));
        return;
    }
    require(listener_).add_poll(handler);
}
void Caps::register_external(const Name& name, Name url) {
    const auto& k = key(name);
    if (external_.contains(k))
        throw std::invalid_argument("Duplicate external capability");
    external_[k] = std::move(url);
}
bool Caps::try_get_poll(const Name& name, std::shared_ptr<CapabilityPollHandler>& handler) const {
    std::lock_guard lock(poll_mutex_);
    auto found = polls_.find(key(name));
    if (found == polls_.end()) {
        handler = nullptr;
        return false;
    }
    handler = found->second;
    return true;
}
Caps::Polls Caps::get_poll_handlers() const {
    std::lock_guard lock(poll_mutex_);
    return polls_;
}
CapabilityPaths Caps::poll_paths() const {
    CapabilityPaths result;
    for (const auto& item : get_poll_handlers())
        result[item.first] = require(item.second).url;
    return result;
}
CapabilityPaths Caps::details(bool exclude, const Requested* names) const {
    auto result = handlers_->details(exclude, names);
    {
        std::lock_guard lock(poll_mutex_);
        for (const auto& item : polls_) {
            auto name = item.first;
            auto handler = item.second;
            if (!requested(names, name))
                continue;
            auto host = host_;
            auto current = main_();
            auto port = current ? require(main_()).port() : 0;
            std::string protocol = "http";
            if (require(main_()).use_ssl()) {
                host = require(main_()).ssl_common_name();
                port = require(main_()).ssl_port();
                protocol = "https";
            }
            result[name] = protocol + "://" + host.value_or("") + ":" + std::to_string(port) +
                           require(handler).url.value_or("");
        }
    }
    for (const auto& item : external_) {
        auto name = item.first;
        auto url = item.second;
        if (requested(names, name))
            result[name] = url;
    }
    return result;
}
void Caps::append_details_xml(const Requested* names, std::string* output) const {
    handlers_->append_details_xml(names, output);
    {
        std::lock_guard lock(poll_mutex_);
        for (const auto& item : polls_) {
            auto name = item.first;
            auto handler = item.second;
            if (requested(names, name))
                server::CapsHandlers::append_llsd_element(
                    name, base_caps_url_ + require(handler).url.value_or(""), output);
        }
    }
    for (const auto& item : external_) {
        auto name = item.first;
        auto url = item.second;
        if (requested(names, name))
            server::CapsHandlers::append_llsd_element(name, url, output);
    }
}
void Caps::deregister_handlers() {
    for (const auto& name : handlers_->caps())
        handlers_->remove(name);
    auto values = get_poll_handlers();
    for (const auto& item : values)
        require(listener_).remove_poll(require(item.second).url);
    {
        std::lock_guard lock(poll_mutex_);
        polls_.clear();
    }
}
std::shared_ptr<CapabilityActivation> Caps::activation() const {
    std::lock_guard lock(activation_mutex_);
    return activation_;
}
void Caps::dispose(bool) {
    flags = None;
    if (auto active = activation()) {
        deregister_handlers();
        active->dispose();
        std::lock_guard lock(activation_mutex_);
        activation_ = nullptr;
    }
}
void Caps::activate() {
    require(activation()).set();
}
bool Caps::wait_for_activation() {
    return require(activation()).wait(120000);
}
}

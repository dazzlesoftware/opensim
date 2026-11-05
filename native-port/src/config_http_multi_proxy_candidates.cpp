#include "opensim/framework/config_http_multi_proxy_candidates.hpp"
#include "opensim/framework/config_http_environment_proxy.hpp"
namespace opensim::framework {
namespace {
bool separator(char16_t c) {
    return c == u';' || c == u' ' || c == u'\n' || c == u'\r' || c == u'\t';
}
struct Part {
    ConfigUriObjectPtr uri;
    std::size_t consumed;
};
Part part(std::u16string_view text, bool secure, const ConfigUriObjectBackend& backend) {
    const auto length = text.size();
    const int match = secure ? 1 : 2;
    while (true) {
        std::size_t i = 0;
        while (i < text.size() && separator(text[i]))
            ++i;
        if (i == text.size())
            break;
        text.remove_prefix(i);
        int kind = 3;
        if (text.starts_with(u"http=")) {
            kind = 2;
            text.remove_prefix(5);
        } else if (text.starts_with(u"https=")) {
            kind = 1;
            text.remove_prefix(6);
        }
        if (text.starts_with(u"http://")) {
            kind = 2;
            text.remove_prefix(7);
        } else if (text.starts_with(u"https://")) {
            kind = 1;
            text.remove_prefix(8);
        }
        i = 0;
        while (i < text.size() && !separator(text[i]))
            ++i;
        if ((kind & match) != 0) {
            if (!backend.try_create)
                throw ConfigHttpProxyProviderError("MultiProxy parser requires Uri.TryCreate provider");
            auto uri = backend.try_create(nini_writer_text(u"http://" + std::u16string(text.substr(0, i))),
                                          ConfigUriObjectKind::Absolute);
            if (uri)
                return {uri, length - text.size() + i};
        }
        text.remove_prefix(i);
    }
    return {{}, length};
}
}
ConfigHttpMultiProxyCandidates ConfigHttpMultiProxyCandidates::parse(ConfigUriText text, bool secure,
                                                                     ConfigUriObjectBackend backend) {
    ConfigHttpMultiProxyCandidates result;
    result.backend_ = std::move(backend);
    std::u16string_view remaining = text ? *text : std::u16string_view{};
    while (true) {
        auto next = part(remaining, secure, result.backend_);
        if (!next.uri)
            break;
        result.uris_.push_back(next.uri);
        remaining.remove_prefix(next.consumed);
    }
    return result;
}
ConfigHttpMultiProxyCandidates ConfigHttpMultiProxyCandidates::lazy(ConfigUriText text, bool secure,
                                                                    ConfigUriObjectBackend backend) {
    ConfigHttpMultiProxyCandidates result;
    result.backend_ = std::move(backend);
    result.secure_ = secure;
    if (text && !text->empty())
        result.text_ = std::move(text);
    return result;
}
ConfigHttpMultiProxyCandidate ConfigHttpMultiProxyCandidates::next() {
    if (!text_) {
        if (current_ == uris_.size())
            return {};
        auto uri = uris_[current_++];
        return {true, uri, current_ == uris_.size()};
    }
    if (current_ < text_->size()) {
        auto value = part(std::u16string_view(*text_).substr(current_), secure_, backend_);
        current_ += value.consumed;
        return {bool(value.uri), value.uri, current_ == text_->size()};
    }
    return {};
}
}

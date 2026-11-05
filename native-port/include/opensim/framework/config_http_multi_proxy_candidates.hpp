// .NET8 Windows MultiProxy Parse/CreateLazy/ReadNextHelper candidate stream.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include <optional>
#include <vector>
namespace opensim::framework {
struct ConfigHttpMultiProxyCandidate {
    bool found = false;
    ConfigUriObjectPtr uri;
    bool is_final = false;
};
// This candidate parser does not mark failures or choose renewal candidates.
// The source FailedProxyCache/ReadNext policy belongs above this stream.
class ConfigHttpMultiProxyCandidates {
    std::vector<ConfigUriObjectPtr> uris_;
    ConfigUriText text_;
    bool secure_ = false;
    std::size_t current_ = 0;
    ConfigUriObjectBackend backend_;

public:
    static ConfigHttpMultiProxyCandidates parse(ConfigUriText, bool, ConfigUriObjectBackend);
    static ConfigHttpMultiProxyCandidates lazy(ConfigUriText, bool, ConfigUriObjectBackend);
    ConfigHttpMultiProxyCandidate next();
    std::size_t position() const {
        return current_;
    }
};
}

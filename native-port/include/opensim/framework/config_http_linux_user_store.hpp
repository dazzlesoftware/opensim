#pragma once
#include "opensim/framework/config_http_linux_certificate.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

namespace opensim::framework {
#ifdef __linux__
// A single CurrentUser directory-store cache. The path is captured at creation;
// immutable snapshots remain valid after refresh, as do up-ref'd source stacks.
class ConfigHttpLinuxUserStore {
    struct State;
    std::unique_ptr<State> state_;

  public:
    using Snapshot = std::shared_ptr<const std::vector<CertificateDer>>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    explicit ConfigHttpLinuxUserStore(std::filesystem::path directory,
                                      Clock clock = std::chrono::steady_clock::now);
    ~ConfigHttpLinuxUserStore();
    Snapshot collection();
    void refresh();
};
#endif
}

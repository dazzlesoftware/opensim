#pragma once
#include "opensim/framework/config_http_linux_user_store.hpp"
namespace opensim::framework {
#ifdef __linux__
struct ConfigHttpLinuxSystemStoreSnapshot {
    std::vector<CertificateDer> roots, intermediates;
};
class ConfigHttpLinuxSystemStore {
    struct State;
    std::unique_ptr<State> state_;

  public:
    using Snapshot = std::shared_ptr<const ConfigHttpLinuxSystemStoreSnapshot>;
    using Clock = ConfigHttpLinuxUserStore::Clock;
    explicit ConfigHttpLinuxSystemStore(Clock clock = std::chrono::steady_clock::now);
    ~ConfigHttpLinuxSystemStore();
    Snapshot collection();
};
#endif
}

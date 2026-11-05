#include "opensim/framework/config_http_linux_certificate_persistence.hpp"
#include "opensim/framework/config_http_linux_trust_stores.hpp"
#include <fstream>
#include <iostream>
#include <array>
using namespace opensim::framework;
int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    auto root = config_http_linux_user_store_root();
    const auto now = std::chrono::steady_clock::now();
    auto clock = [&] { return now; };
    ConfigHttpLinuxUserStore roots(root / "root", clock), ca(root / "ca", clock), my(root / "my", clock);
    std::array<ConfigHttpLinuxUserStore*, 3> stores{&roots, &ca, &my};
    std::array<ConfigHttpLinuxUserStore::Snapshot, 3> before;
    for (int i = 0; i < 3; ++i)
        before[i] = stores[i]->collection();
    ConfigHttpLinuxAiaAssetResult result{std::stoi(argv[1]), {}};
    for (int i = 2; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
        auto cert = config_http_linux_parse_downloaded_certificate(bytes);
        if (!cert)
            return 3;
        result.downloaded.push_back(std::move(*cert));
    }
    config_http_linux_persist_aia_issuers(result, root, [&] {
        for (auto* store : stores)
            store->refresh();
    });
    std::array<ConfigHttpLinuxUserStore::Snapshot, 3> after;
    for (int i = 0; i < 3; ++i)
        after[i] = stores[i]->collection();
    for (int i = 0; i < 3; ++i)
        std::cout << (i ? "," : "") << before[i]->size();
    std::cout << ' ';
    for (int i = 0; i < 3; ++i)
        std::cout << (i ? "," : "") << after[i]->size();
    std::cout << ' ';
    for (int i = 0; i < 3; ++i)
        std::cout << (i ? "," : "") << (before[i] != after[i]);
    std::cout << '\n';
}

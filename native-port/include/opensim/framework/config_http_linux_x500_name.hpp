#pragma once
#include <cstdint>
#include <span>
#include <string>
namespace opensim::framework {
#ifdef __linux__
// X500DistinguishedName.Name (Reversed) used by the system trust-store loader.
std::u16string config_http_linux_x500_name(std::span<const std::uint8_t> der);
#endif
}

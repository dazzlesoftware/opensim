#pragma once
#include <cstdint>

namespace opensim::framework {
// CoreCLR 8's nonzero 26-bit identity hash generator. Explicit state supports differential tests.
std::uint32_t dotnet_identity_hash(std::uint32_t &state, std::uint32_t thread_id) noexcept;
// Native logical thread allocation/consumption differs from CLR; numeric IDs are process local.
std::uint32_t dotnet_identity_hash() noexcept;
} // namespace opensim::framework

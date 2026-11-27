// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <string_view>

namespace opensim::framework {
// .NET8 Marvin over complete little-endian UTF16 code units. No text encoding,
// normalization or case folding is performed, including for unpaired surrogates.
std::uint32_t dotnet_string_hash(std::u16string_view, std::uint64_t seed) noexcept;
// All native callers share one lazily generated process-lifetime seed.
std::uint64_t dotnet_string_hash_seed();
std::uint32_t dotnet_string_hash(std::u16string_view);
} // namespace opensim::framework

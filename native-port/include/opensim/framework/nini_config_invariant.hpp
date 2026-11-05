// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/nini_config.hpp"
#include "opensim/framework/nini_streams.hpp"
namespace opensim::framework {
// Concrete configuration dependencies for OpenSim's invariant startup culture
// and selected system ICU. Arbitrary cultures, NLS and globalization-invariant
// mode require explicitly selected providers rather than this fixed binding.
std::shared_ptr<NiniConfigBackend> native_nini_invariant_config_backend(NiniFileStreamProvider file_stream={});
}

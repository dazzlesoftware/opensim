// Source-derived .NET8 SafeFileHandle/AppContextConfigHelper, MIT.
// See third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/runtime_environment.hpp"
namespace opensim::framework {
bool file_stream_boolean_config_value(const RuntimeEnvironmentText&) noexcept;
// Models AppContext.SetSwitch("System.IO.DisableFileLocking", value). Updating
// the switch does not reset SafeFileHandle's already initialized process latch.
void set_file_stream_disable_locking_switch(bool);
// Call at the source's first DisableFileLocking field read, not when creating
// a provider. An explicit host override bypasses the process latch entirely.
bool file_stream_disable_locking(std::optional<bool> override_value={});
}

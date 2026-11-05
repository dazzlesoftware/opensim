// Native .NET8.0.31 Uri.GetCanonicalPath; .NET Foundation MIT license.
#pragma once
#include "opensim/framework/config_uri_state.hpp"
namespace opensim::framework {
std::u16string config_uri_canonical_path(ConfigUriState&,std::u16string destination,std::int32_t format,bool windows);
}

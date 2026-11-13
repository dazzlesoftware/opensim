// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

namespace opensim::network {
struct HttpServerOptions;
bool load_http_pem_certificate(void* context, const HttpServerOptions& options);
}

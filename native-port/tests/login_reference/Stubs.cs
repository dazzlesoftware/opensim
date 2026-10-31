// Logging, unused namespaces, and the proxy helper are the only adapters.
// Actual protocol types are loaded from the repository's tracked dependencies.
using System;
using System.Net;
namespace OpenSim.Server.Base { public sealed class Unused {} }
namespace OpenSim.Server.Handlers.Base { public sealed class Unused {} }
namespace OpenSim.Framework.Servers.HttpServer { public sealed class Unused {} }
namespace OpenSim.Framework {
    public static class Util {
        public static IPEndPoint GetClientIPFromXFF(string header)=>null;
    }
}

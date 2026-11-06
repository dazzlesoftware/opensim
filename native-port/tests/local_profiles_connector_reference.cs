using System;
using System.IO;
using System.Reflection;
using Nini.Config;
using OpenMetaverse;
using OpenSim.Framework.Servers.HttpServer;
using OpenSim.Region.Framework.Scenes;
using OpenSim.Region.CoreModules.ServiceConnectorsOut.Profile;
namespace Mono.Addins
{
    public class ExtensionAttribute : Attribute
    {
        public string Path { get; set; }
        public string NodeName { get; set; }
        public string Id { get; set; }
    }
}
namespace OpenSim.Framework.Console
{
    public class Placeholder
    {
    }
}
namespace OpenSim.Region.Framework.Interfaces
{
    public interface ISharedRegionModule
    {
    }
}
namespace OpenSim.Services.Interfaces
{
    public class GridRegion
    {
    }
}
namespace OpenSim.Region.Framework.Scenes
{
    public class RegionInfo
    {
        public UUID RegionID;
    }
    public class Scene
    {
        public RegionInfo RegionInfo;
    }
}
namespace OpenSim.Framework.Servers
{
    public static class MainServer
    {
        public static Func<IHttpServer> Get;
        public static IHttpServer Instance => Get();
    }
}
class LocalProfilesProgram
{
    static string Decode(string value) =>
        value == "-"   ? null
        : value == "." ? ""
                       : System.Text.Encoding.UTF8.GetString(Convert.FromHexString(value));
    static string T(string text) => text == null ? "N" : Fixture.H(text);
    static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int mode = int.Parse(p[0]), flags = int.Parse(p[1]);
            string trace = "";
            Fixture.Log.Trace = "";
            var server = new ConfigServer();
            if ((flags & 4) != 0)
                server.AddJsonRPCHandler("classified_update", (OpenMetaverse.StructuredData.OSDMap req,
                                                               ref JsonRpcResponse response) => true);
            var instance = (OpenSim.Services.Interfaces.IUserProfilesService)
                               System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(
                                   typeof(OpenSim.Services.ProfilesService.UserProfilesService));
            Fixture.PluginOverride = (module, args) =>
            {
                trace += "F:" + T(module) + ":" + T((string)args[1]) + ";";
                if (mode == 1)
                    return null;
                if (mode == 2)
                    throw new Exception("factory failed");
                return instance;
            };
            OpenSim.Framework.Servers.MainServer.Get = () =>
            {
                trace += "S;";
                if ((flags & 2) != 0)
                    throw new Exception("server getter failed");
                return (flags & 1) != 0 ? null : server;
            };
            var module = new LocalUserProfilesServicesConnector();
            var a = new Scene { RegionInfo = new RegionInfo { RegionID = new UUID(
                                                                  "00000000-0000-0000-0000-000000000001") } };
            var b = new Scene { RegionInfo = new RegionInfo { RegionID = new UUID(
                                                                  "00000000-0000-0000-0000-000000000002") } };
            string output = "";
            for (int i = 2; i < p.Length; ++i)
            {
                if (i != 2)
                    output += "/";
                try
                {
                    string token = p[i];
                    if (token[0] == 'I' || token[0] == 'S')
                    {
                        var source = token.Substring(1) == "-"
                                         ? null
                                         : new IniConfigSource(new StringReader(Decode(token.Substring(1))));
                        if (token[0] == 'I')
                            module.Initialise(source);
                        else
                            module.InitialiseService(source);
                    }
                    else
                        switch (token)
                        {
                        case "A":
                            module.AddRegion(a);
                            break;
                        case "B":
                            module.AddRegion(b);
                            break;
                        case "D":
                            module.AddRegion(new Scene { RegionInfo = a.RegionInfo });
                            break;
                        case "N":
                            module.AddRegion(null);
                            break;
                        case "Z":
                            module.AddRegion(new Scene());
                            break;
                        case "R":
                            module.RemoveRegion(a);
                            break;
                        case "T":
                            module.RemoveRegion(b);
                            break;
                        case "U":
                            module.RemoveRegion(null);
                            break;
                        case "C":
                            module.Close();
                            break;
                        case "P":
                            module.PostInitialise();
                            break;
                        case "L":
                            module.RegionLoaded(null);
                            break;
                        }
                    output += "OK";
                }
                catch (Exception e)
                {
                    output += "EX:" + Fixture.H(e.Message);
                }
                var regions = (System.Collections.Generic
                                   .Dictionary<UUID, Scene>)typeof(LocalUserProfilesServicesConnector)
                                  .GetField("regions", BindingFlags.Instance | BindingFlags.NonPublic)
                                  .GetValue(module);
                string ids = "";
                foreach (var entry in regions)
                    ids += entry.Key + ":" +
                           (ReferenceEquals(entry.Value, a)   ? "a"
                            : ReferenceEquals(entry.Value, b) ? "b"
                                                              : "d") +
                           ";";
                output += ":" + (module.Enabled ? "1" : "0") + ":" +
                          (module.ServiceModule == null ? "0" : "1") + ":" + T(module.ConfigName) + ":" + ids;
            }
            string keys = "";
            foreach (var key in server.Methods.Keys)
                keys += key + ";";
            System.Console.WriteLine(output + "|" + Fixture.H(trace) + "|" + Fixture.H(Fixture.Log.Trace) +
                                     "|" + Fixture.H(keys) + "|" + T(module.Name) + ":" +
                                     (module.ReplaceableInterface == null ? "N" : "X"));
        }
    }
}

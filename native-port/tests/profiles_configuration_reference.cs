using System;
using System.IO;
using System.Collections.Generic;
using Nini.Config;
using OpenSim.Services.Interfaces;
using OpenSim.Framework.Servers.HttpServer;
namespace OpenSim.Services.Base
{
    public class ServiceBase
    {
        public ServiceBase(IConfigSource source)
        {
        }
        protected T LoadPlugin<T>(string module, object[] args)
            where T : class => ConfigurationProgram.Storage(module, args) as T;
    }
}
namespace OpenSim.Framework.Servers.HttpServer
{
    public interface IHttpServer
    {
        bool AddJsonRPCHandler(string method, JsonRPCMethod handler);
    }
}
namespace OpenSim.Server.Handlers.Base
{
    public class ServiceConnector
    {
        public string ConfigName { get; protected set; }
        public ServiceConnector(IConfigSource source, IHttpServer server, string name)
        {
        }
    }
}
class ConfigServer : IHttpServer
{
    public Dictionary<string, JsonRPCMethod> Methods = new();
    public bool AddJsonRPCHandler(string name, JsonRPCMethod handler) => Methods.TryAdd(name, handler);
}
class ConfigurationProgram
{
    static int Mode;
    static string Trace;
    static string T(string value) => value == null ? "N" : Fixture.H(value);
    static string Decode(string value) =>
        value == "-"   ? null
        : value == "." ? ""
                       : System.Text.Encoding.UTF8.GetString(Convert.FromHexString(value));
    public static object Storage(string module, object[] args)
    {
        Trace += "D:" + T(module) + ":" + T((string)args[0]) + ";";
        if (Mode == 2)
            throw new Exception("storage factory failure");
        return Mode == 1 ? null : Fixture.Data;
    }
    static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var args = line.Split(' ');
            string op = args[0], name = Decode(args[3]), status, state = "-";
            Mode = int.Parse(args[1]);
            int flags = int.Parse(args[2]);
            Trace = Fixture.Log.Trace = "";
            Fixture.Data = new Store(0);
            Fixture.PluginOverride = (module, pluginArgs) =>
            {
                if (pluginArgs.Length == 1)
                {
                    Trace += "A:" + T(module) + ";";
                    if (Mode == 4)
                        throw new Exception("account factory failure");
                    return null;
                }
                Trace += "S:" + T(module) + ":" + T((string)pluginArgs[1]) + ";";
                if (Mode == 6)
                    throw new Exception("service factory failure");
                return Mode == 5 ? null
                                 : System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(
                                       typeof(OpenSim.Services.ProfilesService.UserProfilesService));
            };
            var config = new IniConfigSource(new StringReader(Decode(args[4]) ?? ""));
            var server = new ConfigServer();
            if ((flags & 4) != 0)
                server.AddJsonRPCHandler("classified_update", (OpenMetaverse.StructuredData.OSDMap request,
                                                               ref JsonRpcResponse response) => true);
            try
            {
                if (op == "S")
                {
                    var service = new OpenSim.Services.ProfilesService.UserProfilesService(
                        (flags & 1) != 0 ? null : config, name);
                    state = service.ProfilesData == null ? "N" : "D";
                }
                else
                {
                    var connector = new OpenSim.Server.Handlers.Profiles.UserProfilesConnector(
                        (flags & 1) != 0 ? null : config, (flags & 2) != 0 ? null : server, name);
                    state = (connector.Enabled ? "1" : "0") + (connector.Server == null ? "0" : "1") +
                            (connector.ServiceModule == null ? "0" : "1") + ":" + T(connector.ConfigName);
                }
                status = "OK";
            }
            catch (Exception ex)
            {
                status = "EX:" + Fixture.H(ex.Message);
            }
            string keys = "";
            foreach (var key in server.Methods.Keys)
                keys += key + ";";
            Console.WriteLine(status + "|" + state + "|" + Fixture.H(Trace) + "|" +
                              Fixture.H(Fixture.Log.Trace.Replace("WARN:", "W:")) + "|" + Fixture.H(keys));
        }
    }
}

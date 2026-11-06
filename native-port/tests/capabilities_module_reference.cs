using System;
using System.Text;
using System.Linq;
using System.Reflection;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using Module = OpenSim.Region.CoreModules.Framework.CapabilitiesModule;
using Scene = OpenSim.Region.Framework.Scenes.Scene;
using Caps = OpenSim.Framework.Capabilities.Caps;
namespace Mono.Addins
{
    public class ExtensionAttribute : Attribute
    {
        public string Path { get; set; }
        public string NodeName { get; set; }
        public string Id { get; set; }
    }
}
namespace OpenSim.Region.Framework.Interfaces
{
    public interface INonSharedRegionModule
    {
    }
    public interface ICapabilitiesModule
    {
    }
}
namespace OpenSim.Framework.Servers.HttpServer
{
    public interface IHttpServer
    {
        uint Port { get; }
    }
    public class PollServiceEventArgs
    {
        public string Url;
    }
}
namespace OpenSim.Framework.Servers
{
    public static class MainServer
    {
        class Server : HttpServer.IHttpServer
        {
            public uint Port => 9000;
        }
        public static HttpServer.IHttpServer Instance;
        public static void Set(bool enabled)
        {
            Instance = enabled ? new Server() : null;
        }
    }
}
public class CapContext
{
    public static CapContext Current;
    public OSDMap command;
    public Module owner = new();
    public Dictionary<string, Scene> scenes = new();
    public List<Caps> objects = new();
    public Dictionary<string, Action<string, string[]>> commands = new();
    public Dictionary<ulong, string> seeds = new() { { 256, "first" } }, saved;
    public string trace = "";
    public int next;
    public uint code = 1;
    public bool mutated;
    public void Log(string op, string label = "", string detail = "")
    {
        trace += op + ":" + label + ":" + detail + ";";
        if (command["failure"].AsString() == op)
            throw new Exception();
        if (!mutated && command["mutation"].AsString() == op)
        {
            mutated = true;
            switch (op)
            {
            case "register-event":
                owner.ActivateCaps(code);
                break;
            case "factory":
                owner.AddRegion(scenes["B"]);
                break;
            case "deregister-event":
                owner.CreateCaps(CapReference.Id(3), 9);
                break;
            case "old-deregister":
                owner.RemoveCaps(CapReference.Id(1), code);
                break;
            case "info":
                seeds[999] = "new";
                break;
            }
        }
    }
}
public class CapLog
{
    public void Debug(object s)
    {
    }
    public void DebugFormat(string s, params object[] args)
    {
    }
    public void WarnFormat(string s, params object[] args)
    {
    }
    public void Info(object s) => CapContext.Current.Log("info", "", CapReference.H(s?.ToString() ?? ""));
}
namespace OpenSim.Framework
{
    public static class Util
    {
        public static void RegionHandleToRegionLoc(ulong h, out uint x, out uint y)
        {
            x = (uint)(h >> 40) & 0xffffff;
            y = (uint)(h >> 8) & 0xffffff;
        }
    }
}
namespace OpenSim.Framework
{
    public class MainConsole
    {
        public static MainConsole Instance = new();
        public Commands Commands = new();
        public void Output(string s) => CapContext.Current.Log("console", "", CapReference.H(s));
    }
    public class Commands
    {
        public void AddCommand(string group, bool shared, string name, string help, string summary,
                               Action<string, string[]> action) => Add(name, help, summary, null, action);
        public void AddCommand(string group, bool shared, string name, string help, string summary,
                               string details,
                               Action<string, string[]> action) => Add(name, help, summary, details, action);
        void Add(string name, string help, string summary, string details, Action<string, string[]> action)
        {
            var c = CapContext.Current;
            c.Log("command", name,
                  CapReference.H(help) + ":" + CapReference.H(summary) + ":" + CapReference.Opt(details));
            c.commands[name] = action;
        }
    }
}
namespace OpenSim.Region.Framework.Scenes
{
    public class SceneManager
    {
        public static SceneManager Instance = new();
        public Scene CurrentScene;
    }
    public class RegionInfo
    {
        public string RegionName, ExternalHostName;
    }
    public class ScenePresence
    {
        public string Name = "Test User";
    }
    public class Events
    {
        Scene scene;
        public Events(Scene s)
        {
            scene = s;
        }
        public void TriggerOnRegisterCaps(UUID a, Caps cap) => CapContext.Current.Log("register-event",
                                                                                      scene.label,
                                                                                      a + ":" + cap.serial);
        public void TriggerOnDeregisterCaps(UUID a, Caps cap) => CapContext.Current.Log("deregister-event",
                                                                                        scene.label,
                                                                                        a + ":" + cap.serial);
    }
    public class Scene
    {
        public string label;
        public RegionInfo RegionInfo;
        public string Name => RegionInfo.RegionName;
        public Events EventManager;
        public bool registered;
        public Scene(string value)
        {
            label = value;
            RegionInfo = new() { RegionName = "Region" + value, ExternalHostName = "host" + value };
            EventManager = new(this);
        }
        public void RegisterModuleInterface<T>(T module)
        {
            CapContext.Current.Log("module-register", label);
            registered = true;
        }
        public void UnregisterModuleInterface<T>(T module)
        {
            CapContext.Current.Log("module-unregister", label);
            registered = false;
        }
        public bool TryGetScenePresence(UUID id, out ScenePresence sp)
        {
            sp = new();
            return true;
        }
    }
}
namespace OpenSim.Framework.Capabilities
{
    public class Handlers
    {
        public string BaseURL;
        public Caps caps;
        public Dictionary<string, string> GetCapsLocalPaths() => new() {
            { "SEED", "/CAPS/" + caps.CapsObjectPath + "0000/" }, { "Test", "/test/" }
        };
    }
    public class Caps
    {
        public int serial, activates, deregisters, disposes;
        public UUID AgentID;
        public string CapsObjectPath;
        public Handlers CapsHandlers;
        public Dictionary<string, string> ExternalCapsHandlers =
            new() { { "External", "https://outside/cap" } };
        public Caps(OpenSim.Framework.Servers.HttpServer.IHttpServer server, string host, uint port,
                    string path, UUID agent, string region)
        {
            var c = CapContext.Current;
            serial = ++c.next;
            AgentID = agent;
            CapsObjectPath = path;
            CapsHandlers = new() { BaseURL = "http://" + host + ":" + port, caps = this };
            c.Log("factory", serial.ToString(),
                  host + ":" + port + ":" + CapReference.Opt(path) + ":" + agent + ":" + region);
            c.objects.Add(this);
        }
        public void DeregisterHandlers()
        {
            CapContext.Current.Log("old-deregister", serial.ToString());
            ++deregisters;
        }
        public void Dispose()
        {
            CapContext.Current.Log("dispose", serial.ToString());
            ++disposes;
        }
        public void Activate()
        {
            CapContext.Current.Log("activate", serial.ToString());
            ++activates;
        }
        public Dictionary<string,
                          OpenSim.Framework.Servers.HttpServer.PollServiceEventArgs> GetPollHandlers() =>
            new() { { "Poll", new() { Url = "/poll/" } } };
    }
}
public class CapReference
{
    public static UUID Id(int n) => new UUID("00000000-0000-0000-0000-00000000000" + n);
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Opt(string s) => s == null ? "-" : H(s);
    static void Main()
    {
        Culture.SetCurrentCulture();
        string line;
        while ((line = System.Console.ReadLine()) != null)
        {
            var c = new CapContext { command = (OSDMap)OSDParser.DeserializeJson(
                                         Encoding.UTF8.GetString(Convert.FromHexString(line))) };
            CapContext.Current = c;
            c.scenes["A"] = new("A");
            c.scenes["B"] = new("B");
            OpenSim.Framework.Servers.MainServer.Set(!c.command["no_server"].AsBoolean());
            OpenSim.Region.Framework.Scenes.SceneManager.Instance.CurrentScene = null;
            string results = "";
            foreach (OSDMap op in (OSDArray)c.command["ops"])
            {
                string method = op["op"].AsString(), value = "OK";
                UUID agent = Id(op.ContainsKey("agent") ? op["agent"].AsInteger() : 1);
                c.code = (uint)(op.ContainsKey("code") ? op["code"].AsInteger() : 1);
                try
                {
                    switch (method)
                    {
                    case "add":
                        c.owner.AddRegion(
                            op["null_scene"].AsBoolean()
                                ? null
                                : c.scenes[op.ContainsKey("scene") ? op["scene"].AsString() : "A"]);
                        break;
                    case "remove_region":
                        c.owner.RemoveRegion(c.scenes["B"]);
                        break;
                    case "seed":
                        var circuit = new AgentCircuitData {
                            AgentID = agent,
                            CapsPath =
                                op.ContainsKey("path")
                                    ? (op["path"].Type == OSDType.Unknown ? null : op["path"].AsString())
                                    : "seed",
                            ChildrenCapSeeds = op["null_seeds"].AsBoolean() ? null : c.seeds
                        };
                        c.owner.SetAgentCapsSeeds(op["null_agent"].AsBoolean() ? null : circuit);
                        break;
                    case "path":
                        value = Opt(c.owner.GetCapsPath(agent));
                        break;
                    case "create":
                        c.owner.CreateCaps(agent, c.code);
                        break;
                    case "remove":
                        c.owner.RemoveCaps(agent, c.code);
                        break;
                    case "activate":
                        c.owner.ActivateCaps(c.code);
                        break;
                    case "get":
                        value = c.owner.GetCapsForUser(c.code)?.serial.ToString() ?? "-";
                        break;
                    case "set_children":
                        c.owner.SetChildrenSeed(agent, op["null_seeds"].AsBoolean() ? null : c.seeds);
                        break;
                    case "get_children":
                        c.saved = c.owner.GetChildrenSeeds(agent);
                        value = c.saved == null
                                    ? "-"
                                    : c.saved.Count + ":" + (ReferenceEquals(c.saved, c.seeds) ? 1 : 0);
                        break;
                    case "mutate_saved":
                        c.saved[512] = "saved";
                        break;
                    case "mutate_seed":
                        c.seeds[512] = "changed";
                        break;
                    case "drop":
                        c.owner.DropChildSeed(
                            agent, (ulong)(op.ContainsKey("handle") ? op["handle"].AsInteger() : 256));
                        break;
                    case "child":
                        value = Opt(c.owner.GetChildSeed(
                            agent, (ulong)(op.ContainsKey("handle") ? op["handle"].AsInteger() : 256)));
                        break;
                    case "dump":
                        c.owner.DumpChildrenSeeds(agent);
                        break;
                    case "select":
                        string selected = op["scene"].AsString();
                        OpenSim.Region.Framework.Scenes.SceneManager.Instance.CurrentScene =
                            selected == "" ? null : c.scenes[selected];
                        break;
                    case "list":
                        typeof(Module)
                            .GetMethod("HandleShowCapsListCommand",
                                       BindingFlags.Instance | BindingFlags.NonPublic)
                            .Invoke(c.owner, new object[] { "", Array.Empty<string>() });
                        break;
                    case "stats":
                        typeof(Module)
                            .GetMethod("HandleShowCapsStatsByCapCommand",
                                       BindingFlags.Instance | BindingFlags.NonPublic)
                            .Invoke(c.owner, new object[] { "", new string[op["count"].AsInteger()] });
                        break;
                    case "user_stats":
                        typeof(Module)
                            .GetMethod("HandleShowCapsStatsByUserCommand",
                                       BindingFlags.Instance | BindingFlags.NonPublic)
                            .Invoke(c.owner, new object[] { "", Array.Empty<string>() });
                        break;
                    case "lifecycle":
                        c.owner.Initialise(null);
                        c.owner.RegionLoaded(null);
                        c.owner.PostInitialise();
                        c.owner.Close();
                        value = c.owner.ReplaceableInterface == null && c.owner.Name == "Capabilities Module"
                                    ? "1"
                                    : "0";
                        break;
                    }
                }
                catch
                {
                    value = "EX";
                }
                results += value + ",";
            }
            var field =
                typeof(Module).GetField("m_capsObjects", BindingFlags.Instance | BindingFlags.NonPublic);
            int count = ((Dictionary<uint, Caps>)field.GetValue(c.owner)).Count;
            string actors = "";
            foreach (var cap in c.objects)
                actors += cap.serial + ":" + cap.AgentID + ":" + Opt(cap.CapsObjectPath) + ":" +
                          cap.activates + ":" + cap.deregisters + ":" + cap.disposes + ";";
            System.Console.WriteLine(results + "|" + H(c.trace) + "|" + actors + "|" + count + "|" +
                                     (c.scenes["A"].registered ? 1 : 0) + (c.scenes["B"].registered ? 1 : 0));
        }
    }
}

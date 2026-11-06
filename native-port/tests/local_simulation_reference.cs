using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Reflection;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using Nini.Config;
using Module = OpenSim.Region.CoreModules.ServiceConnectorsOut.Simulation.LocalSimulationConnectorModule;
using Scene = OpenSim.Region.Framework.Scenes.Scene;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
namespace Mono.Addins
{
    public class ExtensionAttribute : Attribute
    {
        public string Path { get; set; }
        public string NodeName { get; set; }
        public string Id { get; set; }
    }
}
namespace OpenSim.Services.Interfaces
{
    public interface ISimulationService
    {
    }
    public class GridRegion
    {
        public UUID RegionID;
        public string RegionName;
    }
}
namespace OpenSim.Region.Framework.Interfaces
{
    public interface ISharedRegionModule
    {
    }
}
public class LocalContext
{
    public Module owner;
    public OSDMap command;
    public string trace = "";
    public bool mutated;
    public SortedDictionary<string, Scene> scenes = new(StringComparer.Ordinal);
    public AgentData data;
    public AgentPosition position;
    public AgentCircuitData circuit;
    public void Log(string op, string label, string text = "")
    {
        trace += op + ":" + label + ":" + text + ";";
        if (command["failure"].AsString() == op &&
            (!command.ContainsKey("fail_scene") || command["fail_scene"].AsString() == label))
            throw new Exception();
    }
    public void Mutate(string label)
    {
        if (mutated || !command.ContainsKey("mutation"))
            return;
        mutated = true;
        string mode = command["mutation"].AsString();
        if (mode == "add")
            owner.Init(scenes["C"]);
        else if (mode == "remove_self")
            owner.RemoveScene(scenes[label]);
        else if (mode == "remove_other")
            owner.RemoveScene(scenes["B"]);
        else if (mode == "duplicate")
            owner.Init(scenes["D"]);
        else if (mode == "remove_all")
            foreach (var pair in scenes)
                owner.RemoveScene(pair.Value);
    }
}
public class LocalObject : ISceneObject
{
    public LocalContext ctx;
    public string label;
    public LocalObject(LocalContext c, string name)
    {
        ctx = c;
        label = name;
    }
    public virtual LocalObject CloneForNewScene()
    {
        ctx.Log("clone", label);
        return ctx.command["null_clone"].AsBoolean() ? null : new LocalObject(ctx, "clone");
    }
    public string GetStateSnapshot()
    {
        ctx.Log("snapshot", label);
        return ctx.command["null_snapshot"].AsBoolean() ? null : "scripts";
    }
    public void SetState(string state, IScene scene)
    {
        ctx.Log("set-state", label, LocalProbe.Opt(state) + ":" + ((Scene)scene).label);
    }
    public string ToXml2() => "xml";
    public string ExtraToXmlString() => "extra";
    public void ExtraFromXmlString(string value)
    {
    }
    public bool HasGroupChanged
    {
        get => false;
        set {
        }
    }
}
public class Arrival
{
    public LocalContext ctx;
    public string label;
    public void AgentArrivedAtDestination(UUID id)
    {
        ctx.Log("arrival", label, id.ToString());
    }
}
namespace OpenSim.Region.Framework.Scenes
{
    public class RegionInfo
    {
        public UUID RegionID;
        public string RegionName;
        public uint RegionSizeX = 256, RegionSizeY = 256;
        public ulong RegionHandle;
    }
    public class Scene : IScene
    {
        public LocalContext ctx;
        public string label;
        public RegionInfo RegionInfo = new();
        public bool registered;
        public Scene(LocalContext c, string name, int n)
        {
            ctx = c;
            label = name;
            RegionInfo.RegionID = LocalProbe.Id(n);
            RegionInfo.RegionName = name;
            RegionInfo.RegionHandle = (ulong)n * 256;
        }
        public void RegisterModuleInterface<T>(T service)
        {
            ctx.Log("register", label, ReferenceEquals(service, ctx.owner) ? "1" : "0");
            registered = true;
        }
        public void UnregisterModuleInterface<T>(T service)
        {
            ctx.Log("unregister", label, ReferenceEquals(service, ctx.owner) ? "1" : "0");
            registered = false;
        }
        public bool NewUserConnection(AgentCircuitData a, uint flags, GridRegion source, out string reason)
        {
            ctx.Log("connect", label,
                    (a == null ? "-" : a.AgentID.ToString()) + ":" + flags + ":" +
                        (source == null ? "-" : source.RegionID.ToString()));
            reason = label + ":reason";
            return !ctx.command["deny"].AsBoolean();
        }
        public bool IncomingUpdateChildAgent(AgentData a)
        {
            ctx.Log("data", label, a == null ? "-" : a.AgentID.ToString());
            return !ctx.command["deny"].AsBoolean();
        }
        public bool IncomingUpdateChildAgent(AgentPosition a)
        {
            ctx.Log("position", label, a == null ? "-" : a.AgentID + ":" + (int)a.Far);
            if (a != null && ctx.command["mutate_data"].AsBoolean())
                a.Far += 1;
            if (ctx.command["reset_data"].AsBoolean())
                ctx.position = null;
            ctx.Mutate(label);
            return !ctx.command["deny"].AsBoolean();
        }
        public bool QueryAccess(UUID a, string home, bool teleport, Vector3 p, List<UUID> features,
                                out string reason)
        {
            ctx.Log("query", label,
                    a + ":" + LocalProbe.Opt(home) + ":" + (teleport ? 1 : 0) + ":" + p + ":" +
                        (features == null ? "-" : features.Count.ToString()));
            features?.Add(LocalProbe.Id(8));
            reason = label + ":reason";
            return !ctx.command["deny"].AsBoolean();
        }
        public Arrival EntityTransferModule
        {
            get {
                ctx.Log("entity", label);
                return ctx.command["null_transfer"].AsBoolean() ? null
                                                                : new Arrival { ctx = ctx, label = label };
            }
        }
        public bool CloseAgent(UUID id, bool force, string auth)
        {
            ctx.Log("close", label, id + ":" + (force ? 1 : 0) + ":" + LocalProbe.Opt(auth));
            return false;
        }
        public bool IncomingCreateObject(Vector3 p, LocalObject o)
        {
            ctx.Log("object", label, p + ":" + (o == null ? "-" : o.label));
            return !ctx.command["deny"].AsBoolean();
        }
        public ISceneObject DeserializeObject(string xml) => null;
    }
}
public class LocalProbe
{
    public static UUID Id(int n) => new UUID("00000000-0000-0000-0000-00000000000" + n);
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Opt(string s) => s == null ? "-" : H(s);
    static void Main()
    {
        Culture.SetCurrentCulture();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var ctx = new LocalContext { command = (OSDMap)OSDParser.DeserializeJson(
                                             Encoding.UTF8.GetString(Convert.FromHexString(line))) };
            var owner = new Module();
            ctx.owner = owner;
            foreach (var pair in new[] { ("A", 1), ("B", 2), ("C", 3), ("D", 1) })
                ctx.scenes[pair.Item1] = new Scene(ctx, pair.Item1, pair.Item2);
            ctx.data = new AgentData { AgentID = Id(7) };
            ctx.position = new AgentPosition { AgentID = Id(7), Far = 10 };
            ctx.circuit = new AgentCircuitData { AgentID = Id(7) };
            var features = new List<UUID> { Id(4), Id(5) };
            string results = "", reason = "before";
            foreach (OSDMap op in (OSDArray)ctx.command["ops"])
            {
                string method = op["op"].AsString(), label = op["scene"].AsString();
                Scene scene = label == "" ? null : ctx.scenes[label];
                var d = new GridRegion {
                    RegionID = Id(op.ContainsKey("id") ? op["id"].AsInteger() : 1),
                    RegionName = op.ContainsKey("name")
                                     ? (op["name"].Type == OSDType.Unknown ? null : op["name"].AsString())
                                     : "Destination"
                };
                GridRegion dest = op["null_destination"].AsBoolean() ? null : d;
                var context = new EntityTransferContext();
                if (op.ContainsKey("version"))
                    context.OutboundVersion = (float)op["version"].AsReal();
                if (op["null_context"].AsBoolean())
                    context = null;
                string result = "OK";
                try
                {
                    switch (method)
                    {
                    case "initialise":
                        IConfigSource config = null;
                        if (!op["null_config"].AsBoolean())
                            config =
                                op.ContainsKey("name")
                                    ? new IniConfigSource(new StringReader(
                                          "[Modules]\nSimulationServices = " + op["name"].AsString() + "\n"))
                                    : new IniConfigSource();
                        owner.Initialise(config);
                        break;
                    case "init":
                        owner.Init(scene);
                        break;
                    case "remove":
                        owner.RemoveScene(scene);
                        break;
                    case "add":
                        owner.AddRegion(scene);
                        break;
                    case "remove_region":
                        owner.RemoveRegion(scene);
                        break;
                    case "close":
                        owner.Close();
                        break;
                    case "post":
                        owner.PostInitialise();
                        break;
                    case "loaded":
                        owner.RegionLoaded(scene);
                        break;
                    case "service_init":
                        owner.InitialiseService(null);
                        break;
                    case "get":
                        result = ((Scene)owner.GetScene(d.RegionID)).label;
                        break;
                    case "inner":
                        result = ReferenceEquals(owner.GetInnerService(), owner) &&
                                         owner.ReplaceableInterface == null
                                     ? "1"
                                     : "0";
                        break;
                    case "local_id":
                        result = owner.IsLocalRegion(d.RegionID) ? "1" : "0";
                        break;
                    case "local_handle":
                        result = owner.IsLocalRegion((ulong)op["handle"].AsInteger()) ? "1" : "0";
                        break;
                    case "resize":
                        scene.RegionInfo.RegionSizeX = (uint)op["x"].AsInteger();
                        scene.RegionInfo.RegionSizeY = (uint)op["y"].AsInteger();
                        break;
                    case "rekey":
                        scene.RegionInfo.RegionID = d.RegionID;
                        break;
                    case "create":
                        result = owner.CreateAgent(
                                     op["source"].AsBoolean() ? new GridRegion { RegionID = Id(9) } : null,
                                     dest, op["null_data"].AsBoolean() ? null : ctx.circuit, uint.MaxValue,
                                     context, out reason)
                                     ? "1"
                                     : "0";
                        break;
                    case "data":
                        result =
                            owner.UpdateAgent(dest, op["null_data"].AsBoolean() ? null : ctx.data, context)
                                ? "1"
                                : "0";
                        break;
                    case "position":
                        result = owner.UpdateAgent(dest, op["null_data"].AsBoolean() ? null : ctx.position)
                                     ? "1"
                                     : "0";
                        break;
                    case "query":
                        result = owner.QueryAccess(
                                     dest, Id(7),
                                     op.ContainsKey("home") && op["home"].Type != OSDType.Unknown
                                         ? op["home"].AsString()
                                         : null,
                                     !op["cross"].AsBoolean(), new Vector3(1.25f, 2, 30),
                                     op["null_features"].AsBoolean() ? null : features, context, out reason)
                                     ? "1"
                                     : "0";
                        break;
                    case "release":
                        result = owner.ReleaseAgent(d.RegionID, Id(7), null) ? "1" : "0";
                        break;
                    case "close_agent":
                        result = owner.CloseAgent(dest, Id(7),
                                                  op.ContainsKey("auth") ? op["auth"].AsString() : null)
                                     ? "1"
                                     : "0";
                        break;
                    case "object":
                        result = owner.CreateObject(
                                     dest, new Vector3(1.25f, 2, 30),
                                     op["null_object"].AsBoolean() ? null : new LocalObject(ctx, "original"),
                                     op["local"].AsBoolean())
                                     ? "1"
                                     : "0";
                        break;
                    }
                }
                catch
                {
                    result = "EX";
                }
                results += result + ",";
            }
            var flags = BindingFlags.NonPublic | BindingFlags.Instance;
            bool enabled = (bool)typeof(Module).GetField("m_ModuleEnabled", flags).GetValue(owner);
            var map = (Dictionary<UUID, Scene>)typeof(Module).GetField("m_scenes", flags).GetValue(owner);
            string registered = "";
            foreach (var pair in ctx.scenes)
                registered += pair.Key + ":" + (pair.Value.registered ? 1 : 0) + ";";
            string fs = "";
            foreach (var id in features)
                fs += id + ",";
            Console.WriteLine(results + "|" + H(reason) + "|" + H(ctx.trace) + "|" + (enabled ? 1 : 0) + "|" +
                              map.Count + "|" + registered + "|" + fs);
        }
    }
}

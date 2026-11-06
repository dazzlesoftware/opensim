// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Net;
using OpenMetaverse;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
using OpenSim.Services.GridService;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
using OpenMetaverse.StructuredData;
namespace OpenSim.Framework
{
    public class AvatarAppearance
    {
    }
    public class AgentCircuitData
    {
        public UUID AgentID;
        public uint circuitcode, teleportFlags;
        public Vector3 startpos;
    }
}
namespace OpenSim.Services.Interfaces
{
    public class UserAccount
    {
        public UUID PrincipalID, ScopeID;
        public string FirstName = "First", LastName = "Last";
    }
    public interface ISimulationService
    {
        bool QueryAccess(GridRegion destination, UUID agent, string home, bool teleport, Vector3 position,
                         List<UUID> features, EntityTransferContext context, out string reason);
        bool CreateAgent(GridRegion source, GridRegion destination, AgentCircuitData circuit, uint flags,
                         EntityTransferContext context, out string reason);
    }
    public interface IUserAgentService
    {
        bool LoginAgentToGrid(GridRegion source, AgentCircuitData circuit, GridRegion gate,
                              GridRegion destination, bool viaLogin, out string reason);
    }
}
public class LaunchTrace
{
    public int Seed, Attempt;
    public string Events = "";
    public AgentCircuitData Circuit;
    public void Add(string value)
    {
        Events += value + ";";
    }
    public static int B(bool value) => value ? 1 : 0;
    public bool Passed() => Seed % 4 == 0 || Seed % 4 == 1 && Attempt == 2 || Seed % 4 == 3 && Attempt == 3;
}
public class Direct : ISimulationService
{
    LaunchTrace trace;
    string label;
    public Direct(LaunchTrace t, string name)
    {
        trace = t;
        label = name;
    }
    public bool QueryAccess(GridRegion region, UUID agent, string home, bool teleport, Vector3 position,
                            List<UUID> features, EntityTransferContext context, out string reason)
    {
        var packed = (OSDMap)context.Pack();
        bool validContext = packed.Count == 3 && packed["InboundVersion"].Type == OSDType.Real &&
                            packed["InboundVersion"].AsReal() == (double)0.8f &&
                            packed["OutboundVersion"].Type == OSDType.Real &&
                            packed["OutboundVersion"].AsReal() == (double)0.8f &&
                            packed["WearablesCount"].Type == OSDType.Integer &&
                            packed["WearablesCount"].AsInteger() == -1;
        ++trace.Attempt;
        trace.Add(label + "-query:" + region.RegionID + ":" + (int)position.Z + ":" +
                  LaunchTrace.B(home == null && teleport && features.Count == 0 &&
                                agent == LaunchProgram.Id(9) && context.InboundVersion == 0.8f &&
                                context.OutboundVersion == 0.8f && context.WearablesCount == -1 &&
                                validContext));
        context.OutboundVersion = 0.7f;
        context.WearablesCount = trace.Seed;
        reason = "query-" + trace.Attempt;
        if (trace.Seed % 13 == 0)
            throw new Exception();
        return !(trace.Seed % 4 == 1 && trace.Attempt == 1 || trace.Seed % 4 == 3 && trace.Attempt == 2);
    }
    public bool CreateAgent(GridRegion source, GridRegion region, AgentCircuitData circuit, uint flags,
                            EntityTransferContext context, out string reason)
    {
        trace.Add(label + "-create:" + region.RegionID + ":" + flags + ":" + circuit.teleportFlags + ":" +
                  LaunchTrace.B(source == null && circuit == trace.Circuit &&
                                context.OutboundVersion == 0.7f && context.WearablesCount == trace.Seed));
        circuit.startpos.Z += 1;
        reason = "create-" + trace.Attempt;
        if (trace.Seed % 17 == 0)
            throw new Exception();
        return trace.Passed();
    }
}
public class Indirect : IUserAgentService
{
    LaunchTrace trace;
    public Indirect(LaunchTrace t)
    {
        trace = t;
    }
    public bool LoginAgentToGrid(GridRegion source, AgentCircuitData circuit, GridRegion gate,
                                 GridRegion destination, bool viaLogin, out string reason)
    {
        ++trace.Attempt;
        trace.Add("hg:" + destination.RegionID + ":" + (gate.ExternalHostName ?? "-") + ":" + gate.HttpPort +
                  ":" + gate.ServerURI + ":" + gate.ScopeID + ":" + gate.Token + ":" +
                  circuit.teleportFlags + ":" +
                  LaunchTrace.B(source == null && viaLogin && circuit == trace.Circuit));
        if ((trace.Seed & 128) != 0)
        {
            ++gate.InternalEndPoint.Port;
            trace.Add("gate-endpoint:" + gate.InternalEndPoint.Port + ":" +
                      destination.InternalEndPoint.Port);
        }
        reason = "hg-" + trace.Attempt;
        if (trace.Seed % 19 == 0)
            throw new Exception();
        return trace.Passed();
    }
}
public partial class LaunchReference
{
    LaunchTrace trace;
    public LaunchReference(LaunchTrace value, GridService grid, ISimulationService local,
                           ISimulationService remote, IUserAgentService userAgent, string uri)
    {
        trace = value;
        m_GridService = grid;
        m_LocalSimulationService = local;
        m_RemoteSimulationService = remote;
        m_UserAgentService = userAgent;
        m_GatekeeperURL = uri;
    }
    uint Code()
    {
        trace.Add("code");
        return 1234;
    }
    AgentCircuitData MakeAgent(GridRegion region, UserAccount account, AvatarAppearance avatar, UUID session,
                               UUID secure, uint code, Vector3 position, string ip, string viewer,
                               string channel, string mac, string id0)
    {
        trace.Add("make:" + region.RegionID + ":" + code);
        if (trace.Seed % 11 == 0)
            throw new Exception();
        var circuit = new AgentCircuitData { AgentID = LaunchProgram.Id(9), circuitcode = code,
                                             startpos = new(12, 13, 14), teleportFlags = 65536 };
        trace.Circuit = circuit;
        return circuit;
    }
    public string Run(GridRegion destination, GridRegion gate, uint flags)
    {
        try
        {var circuit=LaunchAgentAtGrid(gate,destination,new UserAccount{PrincipalID=LaunchProgram.Id(9),ScopeID=LaunchProgram.Id(8)},null,LaunchProgram.Id(10),LaunchProgram.Id(11),new(12,13,14),"home","viewer","channel","mac","id0",new IPEndPoint(IPAddress.Loopback,1234),(TeleportFlags)flags,out var where,out var reason,out var dest);
            return (circuit == null ? "FAIL" : "OK") + "|" + (dest == null ? "-" : dest.RegionID.ToString()) +
                   "|" +
                   where + "|" + reason + "|" + LaunchTrace.B(circuit != null && circuit == trace.Circuit);
        }
        catch
        {
            return "EX";
        }
    }
}
public static class LaunchProgram
{
    public static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    public static void Main(string[] args)
    {
        OpenSim.Framework.Culture.SetCurrentCulture();
        int seed = int.Parse(args[0]);
        var trace = new LaunchTrace { Seed = seed };
        var config = new Nini.Config.IniConfigSource();
        config.AddConfig("GridService");
        var store = new FaultRegionStore();
        var grid = new GridService(store, config, null);
        GridRegion destination = null;
        for (int i = 1; i <= 3; i++)
        {
            var region = new GridRegion { RegionID = Id(i),
                                          RegionName = "Region" + i,
                                          RegionLocX = 25600 + i * 256,
                                          RegionLocY = 25600,
                                          RegionSizeX = 256,
                                          RegionSizeY = 256,
                                          ScopeID = Id(8),
                                          Token = "source-token",
                                          InternalEndPoint = new IPEndPoint(IPAddress.Any, 9000),
                                          ExternalHostName = "127.0.0.1",
                                          HttpPort = 9000 };
            var row = grid.RegionInfo2RegionData(region);
            row.Data["flags"] = i == 1 && (seed & 16) == 0 ? "0" : "6";
            store.Store(row);
            if (i == 1)
                destination = region;
        }
        var local = new Direct(trace, "local");
        var remote = new Direct(trace, "remote");
        var indirect = new Indirect(trace);
        int mode = seed % 5;
        string uri = seed % 3 == 0   ? "http://GATE.example:8002/"
                     : seed % 3 == 1 ? "https://[::1]:8443/"
                                     : "bad/path";
        var launcher = new LaunchReference(
            trace, (seed & 8) != 0 ? null : grid, mode == 1 || mode == 3 || mode == 4 ? local : null,
            mode == 2 || mode == 3 || mode == 4 ? remote : null, mode == 4 ? indirect : null, uri);
        GridRegion gate = null;
        if ((seed & 64) != 0)
            gate = new GridRegion(destination) { ExternalHostName = "provided.example", HttpPort = 9001,
                                                 ServerURI = "http://provided.example:9001/", ScopeID = Id(7),
                                                 Token = "provided-token" };
        Console.WriteLine(launcher.Run(destination, gate, (uint)(128 + ((seed & 32) != 0 ? 512 : 0))) + "|" +
                          trace.Events);
    }
}

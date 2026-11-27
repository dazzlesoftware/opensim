using System;
using System.Net;
using System.Collections.Generic;
using System.Globalization;
using OpenSim.Framework;
using UUID = System.Guid;

static class Clock
{
    public static double Now;
    public static System.Threading.Timer Timer;
    public static bool Deferred;
    public static Queue<Action> Jobs = new();
}

namespace System.Threading
{
    // Timer delivery is explicit; the complete original cache remains unchanged.
    public sealed class Timer : IDisposable
    {
        readonly TimerCallback callback;
        readonly object state;
        public bool Disposed;

        public Timer(TimerCallback callback, object state, int due, int period)
        {
            if (due != 10000)
                throw new Exception("Unexpected pending-cache purge interval");
            this.callback = callback;
            this.state = state;
            Clock.Timer = this;
        }

        public bool Change(int due, int period) { return true; }
        public void Dispose() { Disposed = true; }
        public void Fire()
        {
            if (!Disposed)
                callback(state);
        }
    }
}

namespace OpenSim.Framework
{
    public static class Util
    {
        public static double GetTimeStampMS() { return Clock.Now; }
        public static void FireAndForget(Action<object> callback, object state)
        {
            if (Clock.Deferred)
                Clock.Jobs.Enqueue(() => callback(state));
            else
                callback(state);
        }
    }
}

enum PacketType { UseCircuitCode, AgentUpdate }

class Header
{
    public uint Sequence;
}

class Packet
{
    public PacketType Type;
    public Header Header = new();
}

class UseCircuitCodePacket : Packet
{
    public class CircuitCodeBlock
    {
        public uint Code = 42;
        public UUID ID = UUID.NewGuid();
        public UUID SessionID = UUID.NewGuid();
    }
    public CircuitCodeBlock CircuitCode = new();
    public UseCircuitCodePacket() { Type = PacketType.UseCircuitCode; }
}

class UDPPacketBuffer
{
    public Packet Packet;
    public IPEndPoint Endpoint;
}

class AgentCircuitData
{
    public string IPAddress = "";
    public uint teleportFlags = 0;
}

class CircuitManager
{
    public AgentCircuitData GetAgentCircuitData(uint code) { return new AgentCircuitData(); }
}

class LoginInfo
{
    public UUID SecureSession;
}

class AuthenticateResponse
{
    public LoginInfo LoginInfo = new();
}

class SceneAgent
{
    public LLClientView ControllingClient;
}

interface IClientAPI
{
    string Name { get; }
    UUID AgentId { get; }
    SceneAgent SceneAgent { get; }
    uint CircuitCode { get; }
    UUID SessionId { get; }
    IPEndPoint RemoteEndPoint { get; }
    int DebugPacketLevel { get; set; }
    void Start();
    void SendRegionHandshake();
}

class LLUDPClient
{
    public bool IsConnected = true;
    public IPEndPoint Endpoint;
    public LLUDPClient(object server, object rates, object throttle, uint code, UUID agent,
        IPEndPoint endpoint, int defaultRto, int maximumRto)
    {
        Endpoint = endpoint;
    }
}

class LLClientView : IClientAPI
{
    readonly TestScene scene;
    public string Name { get { return "Fixture"; } }
    public UUID AgentId { get; set; }
    public LLUDPClient UDPClient;
    public IPEndPoint Endpoint { get { return UDPClient.Endpoint; } }
    public SceneAgent SceneAgent { get; set; }
    public uint CircuitCode { get; private set; }
    public UUID SessionId { get; private set; }
    public IPEndPoint RemoteEndPoint { get { return Endpoint; } }
    public int DebugPacketLevel { get; set; }
    public UUID SecureSessionId;
    public bool DisableFacelights;

    public LLClientView(TestScene scene, object server, LLUDPClient udp, AuthenticateResponse response,
        UUID agent, UUID session, uint code)
    {
        this.scene = scene;
        AgentId = agent;
        SessionId = session;
        CircuitCode = code;
        SecureSessionId = response.LoginInfo.SecureSession;
        UDPClient = udp;
    }

    public void Start()
    {
        ++scene.Owner.Admissions;
        if (scene.Owner.Failure == 2)
            throw new Exception("Admission failed");
        SceneAgent = new SceneAgent { ControllingClient = this };
        scene.Client = this;
    }
    public void SendRegionHandshake() { }
}

class RegionInfo
{
    public string RegionName = "Fixture";
}

class TestScene
{
    public string Name = "Fixture";
    public RegionInfo RegionInfo = new();
    public LLClientView Client;
    public Server Owner;

    public bool TryGetClient(UUID agent, out IClientAPI client)
    {
        client = Client;
        return client != null;
    }

    public bool TryGetClient(IPEndPoint endpoint, out IClientAPI client)
    {
        client = Client != null && Client.Endpoint.Equals(endpoint) ? Client : null;
        return client != null;
    }

    public bool CloseAgent(UUID agent, bool force)
    {
        Client = null;
        if (Owner.Failure == 3)
            throw new Exception("Teardown failed");
        return true;
    }
}

class Logger
{
    public int Errors;
    public void Debug(string message) { }
    public void DebugFormat(string message, params object[] arguments) { }
    public void Warn(string message) { }
    public void WarnFormat(string message, params object[] arguments) { }
    public void ErrorFormat(string message, params object[] arguments) { ++Errors; }
}

class Server : IDisposable
{
    readonly ExpiringCacheOS<IPEndPoint, Queue<UDPPacketBuffer>> m_pendingCache = new(10000);
    readonly CircuitManager m_circuitManager = new();
    readonly Logger m_log = new();
    public TestScene Scene = new();
    public int Failure, Admissions, Acknowledgments, Delivered;
    public UUID SecureSession;
    object ThrottleRates = new();
    object Throttle = new();
    int m_defaultRTO = 1000;
    int m_maxRTO = 3000;
    int DefaultClientPacketDebugLevel = 0;
    bool m_disableFacelights = false;
    long IncomingOrphanedPacketCount;

    public Server() { Scene.Owner = this; }

    bool IsClientAuthorized(UseCircuitCodePacket packet, out AuthenticateResponse response)
    {
        response = new AuthenticateResponse();
        response.LoginInfo.SecureSession = SecureSession;
        return true;
    }

    /*ADD_CLIENT*/

    void SendAckImmediate(IPEndPoint endpoint, uint sequence) { ++Acknowledgments; }
    void FreeUDPBuffer(UDPPacketBuffer buffer) { }

    /*HANDLER*/

    public void PacketReceived(UDPPacketBuffer buffer)
    {
        IPEndPoint endPoint = buffer.Endpoint;
        Packet packet = buffer.Packet;
        /*PACKET_MAPPING*/
        if (packet.Type == PacketType.AgentUpdate)
            ++Delivered;
    }

    public string State()
    {
        bool timer = Clock.Timer != null && !Clock.Timer.Disposed;
        return $"{Admissions},{Acknowledgments},{Delivered},{(Scene.Client != null ? 1 : 0)},{m_log.Errors},{(timer ? 1 : 0)}";
    }

    public void Dispose() { m_pendingCache.Dispose(); }
}

// A rejected native admission maps to the original virtual AddClient contract's
// null result, rather than inventing an unregistered client returned by Start.
class AdmissionPolicyServer : Server
{
    protected override IClientAPI AddClient(uint code, UUID agent, UUID session, IPEndPoint endpoint,
        AuthenticateResponse response)
    {
        if (Failure == 1)
        {
            ++Admissions;
            return null;
        }
        return base.AddClient(code, agent, session, endpoint, response);
    }
}

class Program
{
    static void Observe(string input)
    {
        string[] commands = input.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        int scenario = int.Parse(commands[0]);
        Clock.Now = 0;
        Clock.Timer = null;
        Clock.Deferred = false;
        Clock.Jobs.Clear();
        using var server = new AdmissionPolicyServer();
        var original = new IPEndPoint(IPAddress.Loopback, 12001);
        var replacement = new IPEndPoint(IPAddress.Loopback, 12002);
        var use = new UseCircuitCodePacket();
        if (scenario == 3)
            server.PacketReceived(new UDPPacketBuffer { Endpoint = original, Packet = use });
        server.Failure = scenario;
        var observations = new List<string>();
        foreach (string command in commands[1..])
        {
            if (command == "u")
            {
                ++use.Header.Sequence;
                server.PacketReceived(new UDPPacketBuffer { Endpoint = replacement, Packet = use });
            }
            else if (command == "p")
                server.PacketReceived(new UDPPacketBuffer {
                    Endpoint = replacement, Packet = new Packet { Type = PacketType.AgentUpdate }
                });
            else if (command == "ok")
                server.Failure = 0;
            else if (command == "f")
                Clock.Timer?.Fire();
            else if (command == "d")
                Clock.Deferred = true;
            else if (command == "e")
                replacement = new IPEndPoint(replacement.Address, replacement.Port);
            else if (command == "k")
                server.SecureSession = UUID.NewGuid();
            else if (command == "n")
            {
                if (server.Scene.Client != null)
                    server.Scene.Client.SceneAgent = null;
            }
            else if (command == "r")
            {
                if (Clock.Jobs.Count != 0)
                    Clock.Jobs.Dequeue()();
            }
            else if (command.StartsWith('t'))
                Clock.Now = double.Parse(command[1..], CultureInfo.InvariantCulture);
            else
                throw new Exception("Unknown pending admission command");
            observations.Add(server.State());
        }
        Console.WriteLine(string.Join(';', observations));
        server.Failure = 0;
    }

    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
            Observe(line);
    }
}

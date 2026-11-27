using System;
using System.Net;
using UUID = System.Guid;

interface IClientAPI
{
    SceneAgent SceneAgent { get; }
    uint CircuitCode { get; }
    UUID SessionId { get; }
    IPEndPoint RemoteEndPoint { get; }
    int DebugPacketLevel { get; set; }
    void Start();
}

class SceneAgent
{
    public LLClientView ControllingClient;
}

class LoginInfo
{
    public UUID SecureSession;
}

class AuthenticateResponse
{
    public LoginInfo LoginInfo = new();
}

class TestScene
{
    public IClientAPI Client;
    public int Mode;
    public bool HasCircuit = true;
    public string Trace = "";

    public bool TryGetClient(UUID agent, out IClientAPI client)
    {
        client = Client;
        return client != null;
    }

    public bool CloseAgent(UUID agent, bool force)
    {
        Trace += "disable;disconnect;";
        Client = null;
        if (Mode == 3)
            HasCircuit = false;
        if (Mode == 5)
            throw new InvalidOperationException("Scene teardown failed");
        return true;
    }
}

class LLUDPClient
{
    public uint Code;
    public IPEndPoint Endpoint;

    public LLUDPClient(object server, object rates, object throttle, uint code, UUID agent,
        IPEndPoint endpoint, int defaultRto, int maximumRto)
    {
        Code = code;
        Endpoint = endpoint;
    }
}

class LLClientView : IClientAPI
{
    readonly TestScene scene;
    public SceneAgent SceneAgent { get; private set; }
    public uint CircuitCode { get; private set; }
    public UUID SessionId { get; private set; }
    public IPEndPoint RemoteEndPoint { get; private set; }
    public UUID SecureSessionId;
    public int DebugPacketLevel { get; set; }
    public bool DisableFacelights;

    public LLClientView(TestScene scene, object server, LLUDPClient udp, AuthenticateResponse response,
        UUID agent, UUID session, uint code)
    {
        this.scene = scene;
        CircuitCode = code;
        SessionId = session;
        SecureSessionId = response.LoginInfo.SecureSession;
        RemoteEndPoint = udp.Endpoint;
        SceneAgent = new SceneAgent { ControllingClient = this };
    }

    public void Start()
    {
        scene.Trace += "admit;";
        if (scene.Mode == 2)
            throw new InvalidOperationException("Scene admission failed");
        if (!scene.HasCircuit)
            throw new ArgumentNullException("aCircuit");
        scene.Client = this;
    }
}

class Server
{
    public TestScene Scene = new();
    object ThrottleRates = new();
    object Throttle = new();
    int m_defaultRTO = 1000;
    int m_maxRTO = 3000;
    int DefaultClientPacketDebugLevel = 0;
    bool m_disableFacelights = false;

    /*ADD_CLIENT*/

    public void Observe(int mode)
    {
        Scene.Mode = mode;
        UUID agent = UUID.NewGuid(), session = UUID.NewGuid();
        var authentication = new AuthenticateResponse();
        authentication.LoginInfo.SecureSession = UUID.NewGuid();
        var originalEndpoint = new IPEndPoint(IPAddress.Loopback, 11001);
        var udp = new LLUDPClient(this, ThrottleRates, Throttle, 42, agent, originalEndpoint, 1000, 3000);
        Scene.Client = new LLClientView(Scene, this, udp, authentication, agent, session, 42);
        try
        {
            AddClient(42, agent, session, new IPEndPoint(IPAddress.Loopback, 11002), authentication);
        }
        catch (Exception)
        {
            // Observe retained scene state after the unmodified method throws.
        }
        Console.WriteLine($"{mode}|{Scene.Trace}|{(Scene.Client != null ? 1 : 0)}");
    }
}

class Program
{
    static void Main()
    {
        foreach (int mode in new[] { 0, 2, 3, 5 })
            new Server().Observe(mode);
    }
}

using System;
using System.IO;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework.Servers.HttpServer;
using Caps = OpenSim.Framework.Capabilities.Caps;
public interface IOSHttpRequest
{
    string HttpMethod { get; }
    string ContentType { get; }
    IPEndPoint RemoteIPEndPoint { get; }
    Stream InputStream { get; }
}
public interface IOSHttpResponse
{
    int StatusCode { get; set; }
    byte[] RawBuffer { get; set; }
    void AddHeader(string n, string v);
}
public class SeedLog
{
    public Context ctx;
    public void Debug(string s) => ctx.Log("debug:" + SeedReference.Hex(s));
    public void WarnFormat(string s) => ctx.Log("warn:" + SeedReference.Hex(s));
}
public class RegionInfo
{
    public Context ctx;
    public JsonElement command;
    public uint RegionSizeX
    {
        get {
            ctx.Log("x");
            return command.TryGetProperty("x", out var v) ? v.GetUInt32() : 256;
        }
    }
    public uint RegionSizeY
    {
        get {
            ctx.Log("y");
            return command.TryGetProperty("y", out var v) ? v.GetUInt32() : 256;
        }
    }
}
public class Scene
{
    public RegionInfo RegionInfo;
    public Context ctx;
    public bool allow;
    public bool CheckClient(UUID id, IPEndPoint endpoint)
    {
        ctx.Log("check:" + id + ":" + endpoint);
        return allow;
    }
}
public class Request : IOSHttpRequest
{
    public Context ctx;
    public string HttpMethod { get; set; }
    public string ContentType { get; set; }
    public IPEndPoint RemoteIPEndPoint => new(IPAddress.Loopback, 1234);
    public byte[] body;
    public Stream InputStream
    {
        get {
            ctx.Log("input");
            return new MemoryStream(body);
        }
    }
}
public class Response : IOSHttpResponse
{
    public int StatusCode { get; set; } = 201;
    public byte[] RawBuffer { get; set; } = Encoding.UTF8.GetBytes("initial");
    public string contentType = "initial", retry = "";
    public void AddHeader(string n, string v)
    {
        if (n == "Retry-After")
            retry = v;
    }
}
public partial class SeedMethod
{
    public Caps m_HostCapsObj;
    public Scene m_Scene;
    public string m_regionName = "Region";
    public SeedLog m_log;
}
public class SeedReference
{
    public static string Hex(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Parsed(byte[] body)
    {
        try
        {
            var values = (OSDArray)OSDParser.DeserializeLLSDXml(new MemoryStream(body));
            if (values == null)
                return "NULL";
            string result = "";
            foreach (var value in values)
                result += (value == null ? "-" : Hex(value.AsString())) + ",";
            return result;
        }
        catch
        {
            return "EX";
        }
    }
    public static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var command = JsonDocument.Parse(Convert.FromHexString(line)).RootElement;
            var ctx = new Context();
            ControlledActivation.Current = ctx;
            CapLog.Error = s =>
            {};
            var listener = new Listener(ctx, false, "L");
            OpenSim.Framework.Servers.MainServer.Read = () => listener;
            var caps = new Caps(listener, "host", 9000, "seed",
                                new UUID("00000000-0000-0000-0000-000000000001"), "Region");
            GC.SuppressFinalize(caps);
            foreach (var name in command.GetProperty("caps").EnumerateArray())
                caps.RegisterHandler(name.GetString(), new Handler("/local<&'\""));
            caps.RegisterPollHandler("Poll", new PollServiceEventArgs { Url = "/poll" });
            caps.RegisterHandler("External", "https://external/cap?a=1&b=2");
            if (!command.TryGetProperty("active", out var active) || active.GetBoolean())
                caps.Activate();
            caps.Flags =
                (Caps.CapsFlags)(command.TryGetProperty("flags", out var flags) ? flags.GetUInt32() : 0);
            ctx.trace = "";
            ctx.fail = command.TryGetProperty("failure", out var failure) ? failure.GetString() : "";
            var method = new SeedMethod {
                m_HostCapsObj = caps,
                m_Scene =
                    new Scene { ctx = ctx,
                                allow = !command.TryGetProperty("allow", out var allow) || allow.GetBoolean(),
                                RegionInfo = new RegionInfo { ctx = ctx, command = command } },
                m_log = new SeedLog { ctx = ctx }
            };
            var request = new Request {
                ctx = ctx,
                HttpMethod = command.TryGetProperty("method", out var verb) ? verb.GetString() : "POST",
                ContentType =
                    command.TryGetProperty("type", out var type) ? type.GetString() : "application/llsd+xml",
                body = Convert.FromHexString(command.GetProperty("xml").GetString())
            };
            var response = new Response();
            string result = "OK";
            try
            {
                method.SeedCapRequest(request, response);
            }
            catch
            {
                result = "EX";
            }
            Console.WriteLine(result + "|" + response.StatusCode + "|" +
                              Convert.ToHexString(response.RawBuffer).ToLowerInvariant() + "|" +
                              (uint)caps.Flags + "|" + response.retry + "|" + Hex(response.contentType) +
                              "|" + Hex(ctx.trace) + "|" + Parsed(request.body));
        }
    }
}

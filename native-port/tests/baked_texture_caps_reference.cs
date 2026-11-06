using System;
using System.IO;
using System.Net;
using System.Text;
using System.Xml;
using System.Reflection;
using System.Collections;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Timers;
using OpenMetaverse;
using OpenSim.Framework;
using OpenSim.Framework.Capabilities;
using Timer = ControlledTimer;
class ControlledTimer
{
    System.Timers.Timer timer = new();
    public event ElapsedEventHandler Elapsed;
    public bool AutoReset
    {
        set {
            timer.AutoReset = value;
        }
    }
    public double Interval
    {
        set {
            timer.Interval = value;
        }
    }
    public void Start()
    {
        Probe.Add("T30000");
        if ((Probe.Mode & 64) != 0)
            throw new Exception();
        timer.Start();
    }
    public void Stop()
    {
        Probe.Add("T0");
        if ((Probe.Mode & 16) != 0)
            throw new Exception();
        timer.Stop();
    }
    public void Dispose()
    {
        Probe.Add("D");
        if ((Probe.Mode & 32) != 0)
            throw new Exception();
        timer.Dispose();
    }
}
class Logger
{
    public void ErrorFormat(string f, params object[] args)
    {
        Probe.Add("L");
    }
}
class IOSHttpRequest
{
    public string HttpMethod = "POST";
    public IPEndPoint RemoteIPEndPoint = new(IPAddress.Loopback, 123);
    public Stream InputStream;
    public object Headers;
}
class IOSHttpResponse
{
    public int StatusCode = 200;
    public byte[] RawBuffer;
    public void AddHeader(string k, string v)
    {
    }
}
interface IHttpServer
{
    void RemoveSimpleStreamHandler(string p);
    void AddSimpleStreamHandler(SimpleBinaryHandler h);
}
class Server : IHttpServer
{
    public void RemoveSimpleStreamHandler(string p)
    {
        Probe.Add("R");
        if ((Probe.Mode & 8) != 0)
            throw new Exception();
    }
    public void AddSimpleStreamHandler(SimpleBinaryHandler h)
    {
        Probe.Add("A");
        if ((Probe.Mode & 4) != 0)
            throw new Exception();
    }
}
interface IAssetCache
{
    void Cache(AssetBase a);
}
class AssetCache : IAssetCache
{
    public void Cache(AssetBase a)
    {
        Probe.Add("C");
        Probe.Asset = a;
        if ((Probe.Mode & 2) != 0)
            throw new Exception();
    }
}
class Caps
{
    public bool SSLCaps = false;
    public string HostName = "host";
    public int Port = 9000;
    public IHttpServer HttpListener = new Server();
}
static class Util
{
    public static byte[] UTF8NBGetbytes(string s) => Encoding.UTF8.GetBytes(s);
}
partial class UploadBakedTextureModule
{
    static Logger m_log = new();
}
partial class LLSD
{
}
partial class SimpleBinaryHandler
{
    public int RequestsReceived, RequestsHandled;
    public string m_httMethod;
    public int m_maxDatasize = -1;
    public Auth m_Auth = null;
    public Action<IOSHttpRequest, IOSHttpResponse, byte[]> m_processRequest;
    public SimpleBinaryHandler(string m, string p, Action<IOSHttpRequest, IOSHttpResponse, byte[]> cb)
    {
        m_httMethod = m;
        m_processRequest = cb;
    }
    public int MaxDataSize
    {
        set {
            m_maxDatasize = value;
        }
    }
    protected void ProcessRequest(IOSHttpRequest r, IOSHttpResponse s, byte[] b)
    {
    }
}
class Auth
{
    public bool Authenticate(object h, Action<string, string> a, out HttpStatusCode c)
    {
        c = HttpStatusCode.OK;
        return true;
    }
}
class LLSDSerializeException : Exception
{
    public LLSDSerializeException(string s) : base(s)
    {
    }
}
static class Probe
{
    public static int Mode;
    static string trace;
    public static AssetBase Asset;
    public static void Add(string s)
    {
        trace += "|" + s;
    }
    static string Hex(byte[] s) => Convert.ToHexString(s ?? Array.Empty<byte>()).ToLowerInvariant();
    static string Run(int kind, int mode, int phase)
    {
        Mode = mode;
        trace = "";
        Asset = null;
        var req = new IOSHttpRequest();
        var resp = new IOSHttpResponse();
        var origin = IPAddress.Loopback;
        byte[] body = new byte[] { 114, 97, 119, 0, 255 };
        if (phase == 1)
            req.RemoteIPEndPoint = new(IPAddress.Parse("127.0.0.2"), 456);
        if (phase == 2)
            req.HttpMethod = "GET";
        if (phase == 3)
            body = Array.Empty<byte>();
        if (phase == 4)
            body = new byte[6000001];
        if (phase == 5)
            body = new byte[6000000];
        if (phase == 8)
        {
            origin = IPAddress.IPv6Loopback;
            req.RemoteIPEndPoint = new(origin, 456);
        }
        if (phase == 9)
            req.RemoteIPEndPoint = new(IPAddress.IPv6Loopback, 456);
        if (phase == 10)
        {
            origin = IPAddress.Parse("fe80::1%2");
            req.RemoteIPEndPoint = new(IPAddress.Parse("fe80::1%3"), 456);
        }
        req.InputStream = new MemoryStream(body);
        var cache = (mode & 1) != 0 ? null : new AssetCache();
        try
        {
            if (kind == 0)
                new UploadBakedTextureModule().UploadBakedTexture(
                    req, resp, new UUID("11223344-5566-7788-99aa-000000000001"), new Caps(), cache);
            else
            {
                var up = new BakedTextureUploader(
                    "/upload", new Server(), new UUID("11223344-5566-7788-99aa-000000000001"), cache, origin);
                if (phase == 6 || phase == 7)
                {
                    typeof(BakedTextureUploader)
                        .GetMethod("Timeout", BindingFlags.NonPublic | BindingFlags.Instance)
                        .Invoke(up, new object[] { null, null });
                    resp.StatusCode = 0;
                }
                if (phase != 6)
                {
                    var h = new SimpleBinaryHandler("POST", "/upload", up.process);
                    h.MaxDataSize = 6000000;
                    h.Handle(req, resp);
                }
            }
        }
        catch
        {
            resp.StatusCode = 500;
        }
        string s = Encoding.UTF8.GetString(resp.RawBuffer ?? Array.Empty<byte>());
        string identity =
            Asset == null
                ? "none"
                : (!Asset.FullID.Equals(UUID.Zero) && s.Contains(Asset.FullID.ToString()) ? "1" : "0");
        s = Regex.Replace(s, @"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}",
                          m => m.Value == UUID.ZeroString ? m.Value : "ID");
        string asset = Asset == null
                           ? "none"
                           : Asset.Name + "|" + Asset.Type + "|" + Asset.CreatorID + "|" +
                                 (Asset.Local ? 1 : 0) + (Asset.Temporary ? 1 : 0) + "|" + (int)Asset.Flags +
                                 "|" +
                                 (Asset.Data.Length > 100 ? "bytes:" + Asset.Data.Length : Hex(Asset.Data));
        if (Asset != null && Asset.Data.Length > 100)
            asset = asset[..asset.LastIndexOf('|')] + "|bytes:" + Asset.Data.Length;
        return resp.StatusCode + "|" + Hex(Encoding.UTF8.GetBytes(s)) + trace + "|" + asset +
               "|id-match:" + identity;
    }
    static void Main()
    {
        string s;
        while ((s = Console.ReadLine()) != null)
        {
            var p = Array.ConvertAll(s.Split(' '), int.Parse);
            Console.WriteLine(Run(p[0], p[1], p[2]));
        }
    }
}

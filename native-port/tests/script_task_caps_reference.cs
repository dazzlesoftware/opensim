using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using System.Xml;
using System.Reflection;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework.Capabilities;
using OSDMap = OpenMetaverse.StructuredData.OSDMap;
using Timer = ControlledTimer;
class ControlledTimer
{
    public ControlledTimer(Action<object> cb, object state, int due, int period)
    {
        ScriptTaskReference.Add("T" + due);
    }
    public void Dispose()
    {
        ScriptTaskReference.Add("T0");
    }
}
static class Timeout
{
    public const int Infinite = -1;
}
class Logger
{
    public void Error(string s)
    {
        ScriptTaskReference.Add("L");
    }
    public void InfoFormat(string s)
    {
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
        ScriptTaskReference.Add("R");
    }
    public void AddSimpleStreamHandler(SimpleBinaryHandler h)
    {
        ScriptTaskReference.Add("A");
    }
}
class SceneObjectPart
{
}
class Permissions
{
    public bool CanEditObjectInventory(UUID o, UUID a)
    {
        ScriptTaskReference.Add("I");
        return (ScriptTaskReference.Mode & 2) == 0;
    }
    public bool CanEditScript(UUID i, UUID o, UUID a)
    {
        ScriptTaskReference.Add("S");
        return (ScriptTaskReference.Mode & 4) == 0;
    }
}
class Scene
{
    public Permissions Permissions = new();
    public SceneObjectPart GetSceneObjectPart(UUID id)
    {
        ScriptTaskReference.Add("O");
        if ((ScriptTaskReference.Mode & 8) != 0)
            throw new Exception();
        return (ScriptTaskReference.Mode & 1) == 0 ? new() : null;
    }
}
class HostCaps
{
    public bool SSLCaps = false;
    public string HostName = "host";
    public int Port = 9000;
    public IHttpServer HttpListener = new Server();
    public UUID AgentID = ScriptTaskReference.Id(1);
}
static class Util
{
    public static byte[] UTF8NBGetbytes(string s) => Encoding.UTF8.GetBytes(s);
    public static void SaveAssetToFile(string f, byte[] b)
    {
        ScriptTaskReference.Add("D");
        throw new Exception();
    }
}
class LLSDSerializeException : Exception
{
    public LLSDSerializeException(string s) : base(s)
    {
    }
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
    public bool Authenticate(object h, Action<string, string> a, out HttpStatusCode code)
    {
        code = HttpStatusCode.OK;
        return true;
    }
}
partial class BunchOfCaps
{
    Scene m_Scene = new();
    UUID m_AgentID = ScriptTaskReference.Id(1);
    HostCaps m_HostCapsObj = new();
    bool m_dumpAssetsToFile = false;
    static Logger m_log = new();
    string GetNewCapPath()
    {
        ScriptTaskReference.Add("P");
        return "/upload";
    }
    public void TaskScriptUpdated(UUID i, UUID o, bool r, byte[] data, ref ArrayList errors)
    {
    }
    public delegate void UpdateTaskScript(UUID i, UUID o, bool r, byte[] data, ref ArrayList errors);
}
class ScriptTaskReference
{
    public static int Mode;
    static string Trace;
    public static void Add(string s)
    {
        Trace += "|" + s;
    }
    public static UUID Id(int n) => new(n == 1 ? "11223344-5566-7788-99aa-000000000001"
                                               : "11223344-5566-7788-99aa-000000000002");
    static string Probe(int kind, int mode, int phase)
    {
        Mode = mode;
        Trace = "";
        var req = new IOSHttpRequest();
        var resp = new IOSHttpResponse();
        if (kind == 0)
        {
            var map = new OSDMap();
            map["item_id"] = OSD.FromString(phase == 6 ? "invalid" : Id(1).ToString());
            map["task_id"] = OSD.FromString(phase == 7 ? "invalid" : Id(2).ToString());
            map["is_script_running"] = OSD.FromBoolean(true);
            new BunchOfCaps().UpdateScriptTaskInventory(req, resp, map);
        }
        else
        {
            var up = new BunchOfCaps.TaskInventoryScriptUpdater(Id(1), Id(2), (mode & 1) != 0, "/upload",
                                                                new Server(), IPAddress.Loopback,
                                                                (mode & 128) != 0);
            if ((mode & 16) == 0)
                up.OnUpLoad += (UUID i, UUID o, bool run, byte[] data, ref ArrayList errors) =>
                {
                    Add("U" + (run ? 1 : 0) + ":" + data.Length);
                    if ((mode & 32) != 0)
                        throw new Exception();
                    if ((mode & 64) != 0)
                    {
                        errors.Add("(1,2) syntax <error> & text\r\n\t\"'é 😃");
                        errors.Add("");
                    }
                };
            byte[] body = Encoding.UTF8.GetBytes("default{}");
            if (phase == 1)
                req.RemoteIPEndPoint = new(IPAddress.Parse("127.0.0.2"), 456);
            if (phase == 2)
                req.HttpMethod = "GET";
            if (phase == 3)
                body = Array.Empty<byte>();
            if (phase == 4)
                body = new byte[10000001];
            req.InputStream = new MemoryStream(body);
            if (phase == 5)
            {
                up.Timedout(null);
                resp.StatusCode = 0;
            }
            else
            {
                var h = new SimpleBinaryHandler("POST", "/upload", up.process);
                h.MaxDataSize = 10000000;
                h.Handle(req, resp);
            }
        }
        return resp.StatusCode + "|" +
               Convert.ToHexString(resp.RawBuffer ?? Array.Empty<byte>()).ToLowerInvariant() + Trace;
    }
    static void Main()
    {
        string s;
        while ((s = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(s.Split(' '), int.Parse);
            Console.WriteLine(Probe(a[0], a[1], a[2]));
        }
    }
}

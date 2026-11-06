using System;
using System.Text;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
namespace OpenSim.Services.Interfaces
{
    public class GridRegion
    {
        public UUID RegionID;
        public string ServerURI;
    }
}
public class Logger
{
    public void DebugFormat(string f, params object[] args)
    {
    }
    public void WarnFormat(string f, params object[] args)
    {
    }
}
public static class WebUtil
{
    public static int Profile;
    public static string Trace;
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static OSDMap ServiceOSDRequest(string uri, OSDMap data, string method, int timeout,
                                           bool compressed, bool rpc)
    {
        Trace += (uri == null ? "-" : H(uri)) + ":" + timeout + ":" + method + ":" + (data == null ? 1 : 0) +
                 ":" + (compressed ? 1 : 0) + ":" + (rpc ? 1 : 0) + ";";
        if ((Profile & 1) != 0)
            throw new Exception();
        return new OSDMap { { "success", OSD.FromBoolean(false) } };
    }
}
public partial class LifecycleReference
{
    Logger m_log = new();
    protected virtual string AgentPath() => "agent/";
    class Custom : LifecycleReference
    {
        protected override string AgentPath() => "custom/agent/";
    }
    static string Opt(OSD value) => value.Type == OSDType.Unknown ? null : value.AsString();
    public static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int profile = int.Parse(p[0]);
            WebUtil.Profile = profile;
            WebUtil.Trace = "";
            var args =
                (OSDMap)OSDParser.DeserializeJson(Encoding.UTF8.GetString(Convert.FromHexString(p[1])));
            var handler = (profile & 4) != 0 ? new Custom() : new LifecycleReference();
            var region = new GridRegion { RegionID = new UUID("00000000-0000-0000-0000-000000000008"),
                                          ServerURI = "http://127.0.0.1:9000/base/" };
            var agent = new UUID("00000000-0000-0000-0000-000000000007");
            string status;
            try
            {
                bool ok =
                    args["op"].AsString() == "release"
                        ? handler.ReleaseAgent(region.RegionID, agent, Opt(args["uri"]))
                        : handler.CloseAgent((profile & 2) != 0 ? null : region, agent, Opt(args["auth"]));
                status = ok ? "1" : "0";
            }
            catch
            {
                status = "EX";
            }
            Console.WriteLine(status + "|" + WebUtil.Trace);
        }
    }
}

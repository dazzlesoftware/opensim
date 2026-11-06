using System;
using System.Text;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
namespace OpenSim.Services.Interfaces
{
    public class GridRegion
    {
        public UUID RegionID;
        public int RegionLocX, RegionLocY;
        public string RegionName, ServerURI;
    }
}
public static class WebUtil
{
    public static Func<string, OSDMap, int, bool, OSDMap> Request;
    public static OSDMap PutToServiceCompressed(string u, OSDMap d, int t) => Request(u, d, t, true);
    public static OSDMap PutToService(string u, OSDMap d, int t) => Request(u, d, t, false);
}
public partial class RemoteDataReference
{
    class Log
    {
        public void Warn(string s)
        {
        }
    }
    Log m_log = new();
    bool custom;
    protected virtual string AgentPath() => custom ? "custom/" : "agent/";
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-00000000000" + n);
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static void Main()
    {
        Culture.SetCurrentCulture();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var args =
                (OSDMap)OSDParser.DeserializeJson(Encoding.UTF8.GetString(Convert.FromHexString(line)));
            int profile = args["profile"].AsInteger(), calls = 0;
            var d = new GridRegion { RegionID = Id(9), RegionLocX = 25600, RegionLocY = 51200,
                                     RegionName = "Destination", ServerURI = "http://sim:9000/base/" };
            var p = new AgentData();
            p.Unpack(new OSDMap { { "agent_uuid", OSD.FromString(Id(7).ToString()) },
                                  { "position", OSD.FromString("<1.25, 2, 30>") } },
                     null, null);
            if ((profile & 8) != 0)
                p.GodData = new OSD();
            var context = new EntityTransferContext { OutboundVersion = (float)args["version"].AsReal() };
            string trace = "", result = "";
            var c = new RemoteDataReference { custom = (profile & 16) != 0 };
            WebUtil.Request = (uri, body, timeout, compressed) =>
            {
                trace += H(uri) + ":" + timeout + ":" + (compressed ? 1 : 0) + ":" +
                         H(OSDParser.SerializeJsonString(body, true)) + ";";
                var replies = (OSDArray)args["replies"];
                var reply = replies[Math.Min(calls++, replies.Count - 1)];
                if (reply.Type == OSDType.String && reply.AsString() == "throw")
                    throw new Exception();
                return reply.Type == OSDType.Unknown ? null : (OSDMap)reply;
            };
            for (int i = 0; i < 2; i++)
                try
                {
                    result += (c.UpdateAgent((profile & 1) != 0 ? null : d, (profile & 2) != 0 ? null : p,
                                             (profile & 4) != 0 ? null : context)
                                   ? "1,"
                                   : "0,");
                }
                catch
                {
                    result += "EX,";
                }
            Console.WriteLine(result + "|" + H(trace) + "|0|" + (c.GetScene(Id(9)) == null ? 1 : 0));
        }
    }
}

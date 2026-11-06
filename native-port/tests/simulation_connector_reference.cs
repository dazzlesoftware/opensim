using System;
using System.Text;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
namespace OpenSim.Services.Interfaces
{
    public class GridRegion
    {
        public int RegionLocX, RegionLocY;
        public string RegionName, RawServerURI = "", ServerURI;
        public UUID RegionID;
    }
}
public class ConnectorLogger
{
    public void Debug(object o)
    {
    }
    public void Warn(object o)
    {
    }
    public void Info(object o)
    {
    }
    public void DebugFormat(string f, params object[] a)
    {
    }
    public void WarnFormat(string f, params object[] a)
    {
    }
}
public static partial class ConnectorWeb
{
    public static string first, second;
    public static int calls;
    public static OSDArray trace;
    public static OSDMap ServiceOSDRequest(string uri, OSDMap data, string method, int timeout,
                                           bool compressed, bool rpc, bool keepalive = false)
    {
        trace.Add(new OSDMap { { "uri", OSD.FromString(uri) },
                               { "method", OSD.FromString(method) },
                               { "compressed", OSD.FromBoolean(compressed) },
                               { "keepalive", OSD.FromBoolean(keepalive) },
                               { "data", data } });
        var s = calls++ == 0 ? first : second;
        if (s == "throw")
            throw new Exception("fixture failure");
        return (OSDMap)OSDParser.DeserializeJson(Encoding.UTF8.GetString(Convert.FromHexString(s)));
    }
    public static OSDMap PostToServiceCompressed(string uri, OSDMap data,
                                                 int timeout) => ServiceOSDRequest(uri, data, "POST", timeout,
                                                                                   true, false);
    public static OSDMap PostToService(string uri, OSDMap data, int timeout,
                                       bool rpc) => ServiceOSDRequest(uri, data, "POST", timeout, false, rpc);
    public static string Canonical(string s) => OSDParser.SerializeJsonString(CanonicalizeResults(s), true);
}
public partial class ConnectorReference
{
    ConnectorLogger m_log = new();
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-00000000000" + n);
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static void Main()
    {
        Culture.SetCurrentCulture();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            string mode = p[0];
            int profile = int.Parse(p[1]);
            if (mode == "C")
            {
                Console.WriteLine(
                    H(ConnectorWeb.Canonical(Encoding.UTF8.GetString(Convert.FromHexString(p[2])))));
                continue;
            }
            var destination =
                new GridRegion { RegionID = Id(1), RegionLocX = 25600, RegionLocY = 51200,
                                 RegionName = "Destination 試験", ServerURI = "http://127.0.0.1:9000/base/" };
            var source = new GridRegion { RegionID = Id(2), RegionLocX = -256, RegionLocY = 1024,
                                          RegionName = "Source", RawServerURI = "http://source:9000/raw" };
            var circuit = new AgentCircuitData { AgentID = Id(7),
                                                 firstname = "First",
                                                 lastname = "Last",
                                                 CapsPath = "caps/",
                                                 SessionID = Id(8),
                                                 SecureSessionID = Id(9),
                                                 circuitcode = uint.MaxValue,
                                                 startpos = new(1.25f, 2, 30) };
            if ((profile & 8) != 0)
                circuit.Appearance = new AvatarAppearance();
            var context = new EntityTransferContext { WearablesCount = 3 };
            if ((profile & 4) != 0)
                context.InboundVersion = context.OutboundVersion = 0;
            var features = new List<UUID> { Id(3), Id(4) };
            ConnectorWeb.trace = new();
            ConnectorWeb.calls = 0;
            ConnectorWeb.first = p[2];
            ConnectorWeb.second = p.Length > 3 ? p[3] : p[2];
            string reason = "before", status;
            try
            {
                var connector = new ConnectorReference();
                bool passed;
                if (mode == "Q")
                    passed =
                        connector.QueryAccess((profile & 32) != 0 ? null : destination, Id(7),
                                              (profile & 1) != 0 ? "http://home/" : null, (profile & 2) == 0,
                                              new(1.25f, 2, 30), (profile & 16) != 0 ? null : features,
                                              (profile & 8) != 0 ? null : context, out reason);
                else
                    passed = connector.CreateAgent((profile & 1) != 0 ? source : null,
                                                   (profile & 4) != 0 ? null : destination,
                                                   (profile & 16) != 0 ? null : circuit, uint.MaxValue,
                                                   (profile & 2) != 0 ? null : context, out reason);
                status = passed ? "1" : "0";
            }
            catch
            {
                status = "EX";
            }
            string fs = "";
            foreach (var f in features)
                fs += f + ",";
            Console.WriteLine(status + "|" + H(reason) + "|" +
                              BitConverter.SingleToUInt32Bits(context.InboundVersion).ToString("x") + ":" +
                              BitConverter.SingleToUInt32Bits(context.OutboundVersion).ToString("x") + ":" +
                              context.WearablesCount + "|" + fs + "|" +
                              H(OSDParser.SerializeJsonString(ConnectorWeb.trace, true)));
        }
    }
}

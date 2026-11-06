using System;
using System.Globalization;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
public static class GridNetworkReference
{
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-00000000000" + n);
    public static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var input = line.Split(' ');
            int profile = int.Parse(input[0]);
            var connector = new GridNetworkUserAgent(input[1]);
            var source = new GridRegion { RegionID = Id(2), RegionLocX = -256, RegionLocY = 1024,
                                          RegionName = "Source", RawServerURI = "https://SOURCE/raw" };
            var gate = new GridRegion { RegionID = Id(3), ExternalHostName = "GATE.host", HttpPort = 9001,
                                        ServerURI = "https://GATE/Case" };
            var destination =
                new GridRegion { RegionID = Id(1), RegionLocX = 25600, RegionLocY = 51200,
                                 RegionName = "Destination", ServerURI = "https://Destination/Case" };
            var circuit = new AgentCircuitData { AgentID = Id(7),         SessionID = Id(8),
                                                 SecureSessionID = Id(9), circuitcode = uint.MaxValue,
                                                 firstname = "First",     lastname = "Last",
                                                 CapsPath = "caps/",      startpos = new(1.25f, 2, 30) };
            if ((profile & 64) != 0)
            {
                circuit.ServiceURLs = new System.Collections.Generic.Dictionary<string, object> {
                    { "Large", new string('x', 150000) },
                    { "Unicode", "\u00e9\u4e2d\U0001f600" },
                    { "Bool", true },
                    { "Integer", -7 },
                    { "Null", null }
                };
                circuit.ChildrenCapSeeds = new System.Collections.Generic.Dictionary<ulong, string> {
                    { ulong.MaxValue, "https://seed/Case" }, { 1, null }
                };
                circuit.startfar = 512.25f;
            }
            if ((profile & 2) != 0)
                circuit.Appearance = new AvatarAppearance();
            if ((profile & 8) != 0)
                destination = null;
            if ((profile & 16) != 0)
                gate = null;
            if ((profile & 32) != 0)
                circuit = null;
            string reason = "", status;
            try
            {
                status = connector.LoginAgentToGrid((profile & 1) != 0 ? source : null, circuit, gate,
                                                    destination, (profile & 4) != 0, out reason)
                             ? "1"
                             : "0";
            }
            catch (Exception e)
            {
                status = "E:" + e.GetType().FullName + ":" + e.Message;
            }
            Console.WriteLine(OSDParser.SerializeJsonString(
                new OSDMap { { "status", OSD.FromString(status) }, { "reason", OSD.FromString(reason) } },
                true));
        }
    }
}
public class GridNetworkLogger
{
    public void Debug(object value)
    {
    }
    public void Warn(object value)
    {
    }
    public void DebugFormat(string format, params object[] values)
    {
    }
    public void WarnFormat(string format, params object[] values)
    {
    }
}

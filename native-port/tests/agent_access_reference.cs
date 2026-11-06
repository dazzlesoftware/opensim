using GridRegion = OpenSim.Services.Interfaces.GridRegion;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Net;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
namespace OpenSim.Services.Interfaces
{
    public class GridRegion
    {
        public UUID RegionID;
    }
}
namespace OpenSim.Framework
{
    public static class Util
    {
        public static Encoding UTF8 = Encoding.UTF8;
    }
}
public class IOSHttpResponse
{
    public int StatusCode = 211;
    public bool KeepAlive = true;
    public byte[] RawBuffer = Encoding.UTF8.GetBytes("before");
}
public class Logger
{
    public void DebugFormat(string f, params object[] args)
    {
    }
}
public partial class Reference
{
    Service m_SimulationService;
    Logger m_log = new();
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    public static string Bits(float n) => float.IsNaN(n) ? "N"
                                                         : BitConverter.SingleToUInt32Bits(n).ToString("x");
    class Service
    {
        public int Profile;
        public string Trace = "";
        public bool QueryAccess(GridRegion d, UUID a, string h, bool t, Vector3 p, List<UUID> f,
                                EntityTransferContext c, out string r)
        {
            Trace = d.RegionID + ":" + a + ":" + (h == null ? "-" : H(h)) + ":" + (t ? "1" : "0") + ":" +
                    Bits(p.X) + "," + Bits(p.Y) + "," + Bits(p.Z) + ":" + Bits(c.InboundVersion) + "," +
                    Bits(c.OutboundVersion) + "," + c.WearablesCount + ":";
            foreach (var v in f)
                Trace += v + ",";
            if ((Profile & 2) != 0)
                throw new Exception();
            if ((Profile & 4) != 0)
            {
                if (f.Count > 0)
                    f.RemoveAt(0);
                f.Add(Id(9));
                c.InboundVersion = 12;
                c.OutboundVersion = 13;
                c.WearablesCount = 99;
            }
            r = (Profile & 1) != 0 ? "denied" : "";
            return (Profile & 1) == 0;
        }
    }
    public static void Main()
    {
        Culture.SetCurrentCulture();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            string Decode(string s) => s == "." ? "" : Encoding.UTF8.GetString(Convert.FromHexString(s));
            if (p[0] == "P")
            {
                bool ok = GetParams(Decode(p[1]), out UUID a, out UUID r, out string action);
                Console.WriteLine((ok ? 1 : 0) + "|" + a + "|" + r + "|" + H(action));
                continue;
            }
            if (p[0] == "V")
            {
                try
                {
                    var v = Vector3.Parse(Decode(p[1]));
                    Console.WriteLine(Bits(v.X) + "," + Bits(v.Y) + "," + Bits(v.Z));
                }
                catch
                {
                    Console.WriteLine("E");
                }
                continue;
            }
            int profile = int.Parse(p[1]);
            var service = new Service { Profile = profile };
            var handler = new Reference { m_SimulationService = (profile & 8) != 0 ? null : service };
            var response = new IOSHttpResponse();
            bool failed = false;
            try
            {
                handler.DoQueryAccess((OSDMap)OSDParser.DeserializeJson(Decode(p[2])), response, Id(1),
                                      Id(2));
            }
            catch
            {
                failed = true;
            }
            Console.WriteLine((failed ? 1 : 0) + "|" + response.StatusCode + "|" +
                              (response.KeepAlive ? 1 : 0) + "|" + H(service.Trace) + "|" +
                              Convert.ToHexString(response.RawBuffer).ToLowerInvariant());
        }
    }
}

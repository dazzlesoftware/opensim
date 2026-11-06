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
        public string RegionName;
    }
}
public class IOSHttpResponse
{
    public int StatusCode = 211;
    public byte[] RawBuffer = Encoding.UTF8.GetBytes("before");
    public string ContentType = "before-type";
    public bool KeepAlive = true;
}
namespace OpenSim.Framework
{
    public static class Util
    {
        public static Encoding UTF8 = Encoding.UTF8;
    }
}
public partial class PutReference
{
    public Service m_SimulationService;
    public Log m_log = new();
    public class Log
    {
        public void Warn(string s)
        {
        }
        public void InfoFormat(string s, params object[] args)
        {
        }
    }
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Region(GridRegion r) => r.RegionID + ":" + r.RegionLocX + ":" + r.RegionLocY + ":" +
                                                 (r.RegionName == null ? "-" : H(r.RegionName));
    class Obj : ISceneObject
    {
        string xml, extra;
        bool changed;
        public Obj(string x)
        {
            xml = x;
        }
        public string ToXml2() => xml;
        public string ExtraToXmlString() => extra;
        public void ExtraFromXmlString(string value)
        {
            extra = value;
        }
        public bool HasGroupChanged
        {
            get => changed;
            set => changed = value;
        }
    }
    class Scene : IScene
    {
        public ISceneObject DeserializeObject(string xml) => new Obj(xml);
    }
    public class Service
    {
        public int Profile;
        public string Trace = "";
        public IScene GetScene(UUID id)
        {
            Trace += "scene:" + id + ";";
            if ((Profile & 2) != 0)
                throw new Exception();
            return (Profile & 16) != 0 ? null : new Scene();
        }
        public bool UpdateAgent(GridRegion d, AgentData a, EntityTransferContext c)
        {
            Trace += "data:" + Region(d) + ":";
            Trace += H(OSDParser.SerializeJsonString(a.Pack(c), true)) + ":" +
                     H(OSDParser.SerializeJsonString(c.Pack(), true)) + ";";
            if ((Profile & 4) != 0)
                throw new Exception();
            return (Profile & 1) == 0;
        }
        public bool UpdateAgent(GridRegion d, AgentPosition a)
        {
            Trace +=
                "position:" + Region(d) + ":" + H(OSDParser.SerializeJsonString(a.Pack(null), true)) + ";";
            if ((Profile & 4) != 0)
                throw new Exception();
            return (Profile & 1) == 0;
        }
    }
    class Over : PutReference
    {
        Service service;
        public Over(Service s)
        {
            service = s;
            m_SimulationService = (s.Profile & 8) != 0 ? null : s;
        }
        protected override bool UpdateAgent(GridRegion d, AgentData a)
        {
            service.Trace += "override:" + Region(d) + ":" + a.AgentID + ";";
            if ((service.Profile & 64) != 0)
                throw new Exception();
            return (service.Profile & 1) == 0;
        }
    }
    public static void Main()
    {
        Culture.SetCurrentCulture();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int profile = int.Parse(p[0]);
            var service = new Service { Profile = profile };
            PutReference handler =
                (profile & 32) != 0
                    ? new Over(service)
                    : new PutReference { m_SimulationService = (profile & 8) != 0 ? null : service };
            var response = new IOSHttpResponse();
            bool failed = false;
            try
            {
                handler.DoAgentPut(
                    (OSDMap)OSDParser.DeserializeJson(Encoding.UTF8.GetString(Convert.FromHexString(p[1]))),
                    response);
            }
            catch
            {
                failed = true;
            }
            Console.WriteLine((failed ? 1 : 0) + "|" + response.StatusCode + "|" +
                              (response.KeepAlive ? 1 : 0) + "|" +
                              Convert.ToHexString(response.RawBuffer).ToLowerInvariant() + "|" +
                              H(response.ContentType) + "|" + H(service.Trace));
        }
    }
}

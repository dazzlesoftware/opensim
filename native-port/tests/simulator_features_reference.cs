using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Nini.Config;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using OpenSim.Region.ClientStack.Linden;
using OpenSim.Framework.Servers.HttpServer;
using Caps = OpenSim.Framework.Capabilities.Caps;

namespace Mono.Addins
{
    public class ExtensionAttribute : Attribute
    {
        public string Path { get; set; }
        public string NodeName { get; set; }
        public string Id { get; set; }
    }
}

namespace OpenSim.Region.Framework.Interfaces
{
    public interface INonSharedRegionModule
    {
    }
}

namespace OpenSim.Framework
{
    public static class Util
    {
        public static Encoding UTF8 = Encoding.UTF8;
    }

    public class GridInfo
    {
        public void Set(string k, string v)
        {
            FeatureContext.Current.Log("grid:" + k + ":" + FeatureReference.H(v));
            if (FeatureContext.Current.Failure == "grid")
                throw new Exception();
        }

        public string GridName { set => Set("GridName", value); }
        public string GridNick { set => Set("GridNick", value); }
        public string GridUrl { set => Set("GridURL", value); }
        public string SearchURL { set => Set("search-server-url", value); }
        public string DestinationGuideURL { set => Set("destination-guide-url", value); }

        public string[] GridUrlAlias
        {
            set
            {
                FeatureContext.Current.Log("aliases:" + string.Concat(value.Select(v => FeatureReference.H(v) + ",")));
                if (FeatureContext.Current.Failure == "grid")
                    throw new Exception();
            }
        }
    }
}

namespace OpenSim.Region.Framework.Scenes
{
    public class SceneEvents
    {
        public event Action<UUID, Caps> OnRegisterCaps;
        public int Count => OnRegisterCaps?.GetInvocationList().Length ?? 0;

        public void Fire(Caps caps)
        {
            if (OnRegisterCaps != null)
                foreach (Action<UUID, Caps> callback in OnRegisterCaps.GetInvocationList())
                    try
                    {
                        callback(UUID.Zero, caps);
                    }
                    catch
                    {
                        FeatureContext.Current.Log("event-error");
                    }
        }
    }

    public class Grid
    {
        public string Label;
        public Dictionary<string, object> GetExtraFeatures() => FeatureContext.Current.Extras(Label);
    }

    public class Scene
    {
        public string Label;
        public SceneEvents EventManager = new();
        public int Registered;
        public Grid GridService;
        public GridInfo SceneGridInfo
        {
            get
            {
                FeatureContext.Current.Log("grid-info:" + Label);
                return new();
            }
        }

        public Scene(string label)
        {
            Label = label;
            GridService = new()
            {
                Label = label
            };
        }

        public void RegisterModuleInterface<T>(T module)
        {
            FeatureContext.Current.Log("register:" + Label);
            ++Registered;
        }
    }
}

namespace OpenSim.Framework.Servers.HttpServer
{
    public interface IOSHttpRequest
    {
        string HttpMethod { get; }
    }

    public interface IOSHttpResponse
    {
        int StatusCode { get; set; }

        byte[] RawBuffer { get; set; }
    }

    public delegate void SimpleStreamMethod(IOSHttpRequest request, IOSHttpResponse response);
    public class SimpleStreamHandler : ISimpleStreamHandler
    {
        public string Path { get; }

        public SimpleStreamMethod Callback;
        public SimpleStreamHandler(string path, SimpleStreamMethod callback)
        {
            Path = path;
            Callback = callback;
        }

        public void Handle(IOSHttpRequest request, IOSHttpResponse response)
        {
            try
            {
                Callback(request, response);
            }
            catch
            {
                response.StatusCode = 500;
            }
        }
    }
}

public class FeatureRequest : IOSHttpRequest
{
    public string HttpMethod { get; set; }
}

public class FeatureResponse : IOSHttpResponse
{
    public int StatusCode { get; set; } = 207;
    public byte[] RawBuffer { get; set; } = Encoding.UTF8.GetBytes("old");
}

public class FeatureListener : IHttpServer
{
    public Dictionary<string, ISimpleStreamHandler> Routes = new();
    public bool UseSSL => false;
    public uint Port => 0;
    public uint SSLPort => 0;
    public string SSLCommonName => null;

    public void AddStreamHandler(IRequestHandler h)
    {
    }

    public void RemoveStreamHandler(string m, string p)
    {
    }

    public void AddSimpleStreamHandler(ISimpleStreamHandler h)
    {
        Routes[h.Path] = h;
    }

    public void RemoveSimpleStreamHandler(string p)
    {
        Routes.Remove(p);
    }

    public void AddPollServiceHTTPHandler(PollServiceEventArgs p)
    {
    }

    public void RemovePollServiceHTTPHandler(string p)
    {
    }
}

public class FeatureLog
{
    public void Error(object o)
    {
        FeatureContext.Current.Log("error:" + FeatureReference.H(o.ToString()));
        if (FeatureContext.Current.Failure == "log")
            throw new Exception();
    }

    public void WarnFormat(string s, params object[] args)
    {
        FeatureContext.Current.Log("warn:" + FeatureReference.H(string.Format(s, args)));
    }
}

public static class FeatureFiles
{
    public static bool Exists(string path)
    {
        if (FeatureContext.Current.Profile >= 1000)
            return File.Exists(path);
        FeatureContext.Current.Log("exists");
        return (FeatureContext.Current.Profile / 4) % 6 != 0;
    }

    public static StreamReader OpenText(string path)
    {
        if (FeatureContext.Current.Profile >= 1000)
            return File.OpenText(path);
        FeatureContext.Current.Log("open");
        return new FeatureReader();
    }
}

public class FeatureReader : StreamReader
{
    int reads;
    public FeatureReader() : base(new MemoryStream(Encoding.UTF8.GetBytes(FeatureContext.SyntaxText(FeatureContext.Current.Profile / 4))))
    {
    }

    public override string ReadLine()
    {
        FeatureContext.Current.Log("read" + reads++);
        return base.ReadLine();
    }

    protected override void Dispose(bool disposing)
    {
        FeatureContext.Current.Log("dispose");
        base.Dispose(disposing);
    }
}

public class FeatureContext
{
    public static FeatureContext Current;
    public int Profile, Random;
    public string Failure = "", Trace = "";
    public void Log(string s)
    {
        Trace += s + ";";
        if (s == Failure)
            throw new Exception(s);
    }

    public UUID RandomUUID()
    {
        Log("random");
        return new UUID("00000000-0000-0000-0000-" + (++Random).ToString("D12"));
    }

    public string Text => ((Profile % 8 + Profile / 104) % 8) switch
    {
        0 => null,
        1 => "",
        2 => "<&\"'>",
        3 => "https://example/[USERID]/[USERID]",
        4 => "世界😀",
        5 => "a\0b",
        6 => "plain",
        _ => "a\r\nb"
    };
    public string Key => ((Profile / 13) % 8) switch
    {
        0 => "Test",
        1 => "OpenSimExtras",
        2 => "destination-guide-url",
        3 => "bad&key",
        4 => "",
        5 => null,
        6 => "世界",
        _ => "nul\0key"
    };

    public OSD Value() => (Profile % 13) switch
    {
        0 => null,
        1 => new OSD(),
        2 => OSD.FromBoolean(true),
        3 => OSD.FromInteger(-1),
        4 => OSD.FromString(Text ?? ""),
        5 => new OSDMap
        {
            {
                "nested",
                OSD.FromString("original")
            }
        },
        6 => new OSDArray
        {
            OSD.FromBoolean(true),
            OSD.FromString("text")
        },
        7 => OSD.FromReal(1.0 / 3),
        8 => OSD.FromUUID(new UUID("01234567-89ab-cdef-0123-456789abcdef")),
        9 => OSD.FromLong(-12345678901234),
        10 => OSD.FromBinary(new byte[] { 0, 128, 255 }),
        11 => OSD.FromUri(new Uri("https://example/a?x=1&y=2")),
        _ => OSD.FromDate(new DateTime(2000, 1, 1, 0, 0, 0, DateTimeKind.Utc))};
    public static string SyntaxText(int p) => (p % 6) switch
    {
        0 => "",
        1 => " 01234567-89ab-cdef-0123-456789abcdef \r\n <!--skip--> \n<map>\n \t \n<string>syntax</string>\r</map>\n",
        2 => "invalid\npayload",
        3 => "",
        4 => "00000000-0000-0000-0000-000000000000\nzero",
        _ => "01234567-89ab-cdef-0123-456789abcdef\n"
    };
    public Dictionary<string, object> Extras(string label)
    {
        Log("extras:" + label);
        var d = new Dictionary<string, object>();
        switch (Profile % 10)
        {
            case 1:
                foreach (var pair in new[]
                {
                    ("GridName", "grid"),
                    ("GridNick", "nick"),
                    ("GridURL", "uri"),
                    ("GridURLAlias", "one,,two"),
                    ("search-server-url", "search"),
                    ("destination-guide-url", "guide"),
                    ("currency-base-uri", "ignored"),
                    ("Custom", "text")
                }

                )
                    d[pair.Item1] = pair.Item2;
                break;
            case 2:
                d["Result"] = "Failure";
                break;
            case 3:
                d["Result"] = "Success";
                d["ExportSupported"] = " TrUe ";
                break;
            case 4:
                d["Result"] = null;
                break;
            case 5:
                d["Custom"] = 12;
                break;
            case 6:
                d["Custom"] = "kept";
                d["GridURLAlias"] = null;
                break;
            case 7:
                d["ExportSupported"] = "nonsense";
                break;
            case 8:
                d["Result"] = new ResultObject();
                break;
            case 9:
                d["ExportSupported"] = " true\0 ";
                break;
        }

        return d;
    }

    class ResultObject
    {
        public override string ToString()
        {
            Current.Log("to-string");
            return "Failure";
        }
    }
}

public class FeatureReference
{
    public static string H(string s) => s == null ? "-" : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Hex(byte[] b) => b == null ? "-" : Convert.ToHexString(b).ToLowerInvariant();
    static string Dump(OSD v)
    {
        if (v == null)
            return "-";
        try
        {
            return Hex(OSDParser.SerializeLLSDXmlToBytes(v));
        }
        catch
        {
            return "EX";
        }
    }

    static BindingFlags Private = BindingFlags.NonPublic | BindingFlags.Instance;
    static object Field(object m, string name) => m.GetType().GetField(name, Private).GetValue(m);
    static string ConfigRawHex(string s) => s.Length == 0 ? "-" : string.Concat(s.Select(c => ((int)c).ToString("x4")));
    static string ConfigRaw(string s)
    {
        if (s == "-")
            return "";
        var a = new char[s.Length / 4];
        for (int i = 0; i < a.Length; i++)
            a[i] = (char)Convert.ToUInt16(s.Substring(i * 4, 4), 16);
        return new string (a);
    }

    static string ConfigFlow(string line)
    {
        var a = line.Split(' ');
        int aliases = int.Parse(a[0]);
        var context = new FeatureContext();
        FeatureContext.Current = context;
        var type = typeof(SimulatorFeaturesModule);
        type.GetField("m_scriptSyntaxID", BindingFlags.NonPublic | BindingFlags.Static).SetValue(null, UUID.Zero);
        type.GetField("m_scriptSyntaxXML", BindingFlags.NonPublic | BindingFlags.Static).SetValue(null, null);
        var module = new SimulatorFeaturesModule();
        var results = new List<string>();
        foreach (var encoded in a.Skip(1))
        {
            string result = "OK";
            try
            {
                if (encoded == "~")
                    module.Initialise(null);
                else
                {
                    var source = new IniConfigSource(new StringReader(ConfigRaw(encoded)));
                    void Add(AliasText owner)
                    {
                        owner.AddAlias("On", true);
                        owner.AddAlias("Yes", true);
                        owner.AddAlias("Off", false);
                        owner.AddAlias("No", false);
                    }

                    if (aliases == 1)
                        Add(source.Alias);
                    if (aliases == 2)
                    {
                        var argv = new ArgvConfigSource(Array.Empty<string>());
                        Add(argv.Alias);
                        source.Merge(argv);
                    }

                    if (aliases == 3 && source.Configs["SimulatorFeatures"] != null)
                        Add(source.Configs["SimulatorFeatures"].Alias);
                    module.Initialise(source);
                }
            }
            catch (Exception e)
            {
                result = "E:" + e.GetType().FullName + ":" + unchecked((uint)e.HResult) + ":" + (e is ArgumentException arg && arg.ParamName != null ? ConfigRawHex(arg.ParamName) : "~") + ":" + ConfigRawHex(e.Message);
            }

            results.Add(result + ":" + ((bool)Field(module, "m_ExportSupported") ? 1 : 0) + ((bool)Field(module, "m_doScriptSyntax") ? 1 : 0));
        }

        return string.Join("/", results) + "|" + H(context.Trace);
    }

    public static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--config-probe")
        {
            System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
            System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
            for (string input; (input = Console.ReadLine()) != null;)
                Console.WriteLine(ConfigFlow(input));
            return;
        }

        string line;
        while ((line = Console.ReadLine()) != null)
        {
            int first = line.IndexOf(':'), last = line.IndexOf(':', first + 1);
            var c = new FeatureContext
            {
                Profile = int.Parse(line[..first]),
                Failure = line[(first + 1)..last]
            };
            FeatureContext.Current = c;
            var type = typeof(SimulatorFeaturesModule);
            type.GetField("m_scriptSyntaxID", BindingFlags.NonPublic | BindingFlags.Static).SetValue(null, UUID.Zero);
            type.GetField("m_scriptSyntaxXML", BindingFlags.NonPublic | BindingFlags.Static).SetValue(null, null);
            ControlledActivation.Current = new Context();
            CapLog.Error = s =>
            {
            };
            var listener = new FeatureListener();
            OpenSim.Framework.Servers.MainServer.Read = () => listener;
            var caps = new Caps(listener, "host", 0, "seed", new UUID("00000000-0000-0000-0000-000000000007"), "Region");
            GC.SuppressFinalize(caps);
            if ((c.Profile & 2) != 0)
                caps.Flags = Caps.CapsFlags.TPBR;
            var module = new SimulatorFeaturesModule();
            var a = new OpenSim.Region.Framework.Scenes.Scene("A");
            var b = new OpenSim.Region.Framework.Scenes.Scene("B");
            OSD original = c.Value(), saved = null;
            int config = c.Profile % 4;
            string output = "";
            foreach (char op in line[(last + 1)..])
            {
                string result = "OK";
                try
                {
                    switch (op)
                    {
                        case 'I':
                            module.Initialise(new IniConfigSource(Path.Combine(args[0], "config-" + config + ".ini")));
                            break;
                        case 'N':
                            module.Initialise(null);
                            break;
                        case 'J':
                            config ^= 3;
                            break;
                        case 'A':
                            module.AddRegion(a);
                            break;
                        case 'B':
                            module.AddRegion(b);
                            break;
                        case 'R':
                            module.RemoveRegion(a);
                            break;
                        case 'L':
                            module.RegionLoaded(a);
                            break;
                        case 'M':
                            module.RegionLoaded(b);
                            break;
                        case 'C':
                            module.RegisterCaps(UUID.Zero, caps);
                            break;
                        case 'Q':
                            module.RegisterCaps(UUID.Zero, null);
                            break;
                        case 'E':
                            a.EventManager.Fire(caps);
                            break;
                        case 'F':
                            module.AddFeature(c.Key, original);
                            break;
                        case 'X':
                            module.AddOpenSimExtraFeature(c.Key, original);
                            break;
                        case 'D':
                            result = module.RemoveFeature(c.Key) ? "1" : "0";
                            break;
                        case 'T':
                            result = (module.TryGetFeature(c.Key, out OSD v) ? "1:" : "0:") + Dump(v);
                            break;
                        case 'U':
                            result = (module.TryGetOpenSimExtraFeature(c.Key, out OSD x) ? "1:" : "0:") + Dump(x);
                            break;
                        case 'V':
                            result = module.OpenSimExtraFeatureContains(c.Key) ? "1" : "0";
                            break;
                        case 'G':
                            result = Dump(module.GetFeatures());
                            break;
                        case 'S':
                            module.TryGetFeature(c.Key, out saved);
                            break;
                        case 'm':
                            if (saved is OSDMap map)
                                map["nested"] = OSD.FromInteger(8);
                            else if (saved is OSDArray array)
                                array.Add(OSD.FromString("changed"));
                            else if (saved is OSDBinary)
                                saved.AsBinary()[0] = 17;
                            break;
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                            int id = op - '0';
                            module.OnSimulatorFeaturesRequest += (UUID agent, ref OSDMap copy) =>
                            {
                                c.Log("callback" + id);
                                if (id == 1)
                                    copy["Added"] = true;
                                if (id == 2)
                                    copy = new OSDMap
                                    {
                                        {
                                            "Replacement",
                                            1
                                        }
                                    };
                                if (id == 3)
                                {
                                    copy["Partial"] = true;
                                    throw new Exception();
                                }

                                if (id == 4)
                                    copy = null;
                            };
                            break;
                        case 'H':
                        case 'P':
                        case 'Y':
                        case 'Z':
                            var request = new FeatureRequest
                            {
                                HttpMethod = op == 'P' || op == 'Z' ? "POST" : "GET"
                            };
                            var response = new FeatureResponse();
                            string ok = "OK";
                            try
                            {
                                var method = type.GetMethod(op == 'Y' || op == 'Z' ? "HandleSyntaxRequest" : "HandleSimulatorFeaturesRequest", Private);
                                method.Invoke(module, op == 'Y' || op == 'Z' ? new object[] { request, response } : new object[] { request, response, caps });
                            }
                            catch
                            {
                                ok = "EX";
                            }

                            result = ok + ":" + response.StatusCode + ":" + Hex(response.RawBuffer);
                            break;
                        case 'h':
                            var paths = caps.CapsHandlers.GetCapsLocalPaths();
                            var route = (SimpleStreamHandler)listener.Routes[paths["SimulatorFeatures"]];
                            var reply = new FeatureResponse
                            {
                                StatusCode = 200,
                                RawBuffer = null
                            };
                            route.Handle(new FeatureRequest { HttpMethod = "GET" }, reply);
                            result = reply.StatusCode + ":" + Hex(reply.RawBuffer ?? Array.Empty<byte>());
                            break;
                        case 'x':
                            module.AddFeature("Raw", new OSDllsdxml("<map><key>raw</key><integer>7</integer></map>"));
                            break;
                        case 'z':
                            module.AddFeature("Utf8", new OSDUTF8(new osUTF8("text")));
                            break;
                        case 'O':
                            module.Close();
                            result = module.Name;
                            break;
                    }
                }
                catch
                {
                    result = "EX";
                }

                output += result + ",";
            }

            var routes = caps.CapsHandlers.GetCapsLocalPaths().Select(v => v.Key + ":" + v.Value).OrderBy(v => v, StringComparer.Ordinal);
            Console.WriteLine(output + "|" + Dump((OSD)Field(module, "m_features")) + "|" + ((bool)Field(module, "m_ExportSupported") ? 1 : 0) + ((bool)Field(module, "m_doScriptSyntax") ? 1 : 0) + "|" + type.GetField("m_scriptSyntaxID", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null) + ":" + Hex((byte[])type.GetField("m_scriptSyntaxXML", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null)) + "|" + a.Registered + ":" + a.EventManager.Count + ":" + b.Registered + ":" + b.EventManager.Count + "|" + string.Concat(routes.Select(v => v + ",")) + "|" + H(c.Trace));
            caps.Dispose();
        }
    }
}

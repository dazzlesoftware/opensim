using System;
using System.Text;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;

class CircuitProgram
{
    class Custom
    {
        public override string ToString() => "custom-value";
    }

    class Counter
    {
        int n = 0;
        public override string ToString() => (++n).ToString();
    }

    class Mutating
    {
        public Dictionary<string, object> urls;
        public override string ToString()
        {
            urls["added"] = "value";
            return "changed";
        }
    }

    class Switching
    {
        public Action change;
        public override string ToString()
        {
            change();
            return "old-value";
        }
    }

    static string Hex(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Unhex(string s) => s == "." ? "" : Encoding.UTF8.GetString(Convert.FromHexString(s));
    static string Canonical(OSD v)
    {
        switch (v.Type)
        {
            case OSDType.Integer:
                return "i" + v.AsInteger();
            case OSDType.Boolean:
                return v.AsBoolean() ? "b1" : "b0";
            case OSDType.String:
                return "s" + Hex(v.AsString());
            case OSDType.UUID:
                return "q" + v.AsUUID();
            case OSDType.Real:
                return "r" + (double.IsNaN(v.AsReal()) ? 0x7ff8000000000000ul : BitConverter.DoubleToUInt64Bits(v.AsReal())).ToString("x");
            case OSDType.Binary:
                return "x" + Convert.ToHexString(v.AsBinary()).ToLowerInvariant();
            case OSDType.Array:
            {
                string s = "[";
                foreach (OSD item in (OSDArray)v)
                    s += Canonical(item) + ",";
                return s + "]";
            }

            case OSDType.Map:
            {
                var sorted = new SortedDictionary<string, OSD>(StringComparer.Ordinal);
                foreach (KeyValuePair<string, OSD> p in (OSDMap)v)
                    sorted[p.Key] = p.Value;
                string s = "{";
                foreach (var p in sorted)
                    s += Hex(p.Key) + "=" + Canonical(p.Value) + ",";
                return s + "}";
            }

            default:
                return "u";
        }
    }

    static void Main(string[] args)
    {
        Culture.SetCurrentCulture();
        if (args.Length > 0 && args[0] == "ignorables")
        {
            int start = -1, last = -1;
            for (int n = 0; n <= 0x10ffff; n++)
            {
                bool equal = n < 0xd800 || n > 0xdfff ? string.Equals("fa" + char.ConvertFromUtf32(n) + "lse", "false", StringComparison.InvariantCultureIgnoreCase) : false;
                if (equal)
                {
                    if (start < 0)
                        start = n;
                    last = n;
                }
                else if (start >= 0)
                {
                    Console.WriteLine(start.ToString("x") + " " + last.ToString("x"));
                    start = -1;
                }
            }

            if (start >= 0)
                Console.WriteLine(start.ToString("x") + " " + last.ToString("x"));
            return;
        }

        var c = new AgentCircuitData();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            string op = p[0], status = "OK", output = "-";
            try
            {
                if (op == "reset")
                    c = new AgentCircuitData();
                else if (op == "empty")
                    c.UnpackAgentCircuitData(new OSDMap());
                else if (op == "null-strings")
                {
                    c.CapsPath = null;
                    c.ServiceSessionID = null;
                    c.ChildrenCapSeeds = new Dictionary<ulong, string>
                    {
                        {
                            42,
                            null
                        }
                    };
                }
                else if (op == "precedence")
                    c.UnpackAgentCircuitData(new OSDMap { { "appearance_serial", OSD.FromInteger(99) }, { "packed_appearance", new OSDMap { { "serial", OSD.FromInteger(17) } } }, { "service_urls", new OSDArray { OSD.FromString("old"), OSD.FromString("obsolete") } }, { "serviceurls", new OSDMap { { "new", OSD.FromString("current") } } } });
                else if (op == "set")
                {
                    string key = p[1];
                    OSD value = FixtureValue.Build(line.Substring(line.IndexOf(' ', line.IndexOf(' ') + 1) + 1));
                    var map = new OSDMap();
                    if (key == "all")
                        foreach (string k in new[]
                        {
                            "agent_id",
                            "base_folder",
                            "caps_path",
                            "child",
                            "circuit_code",
                            "first_name",
                            "last_name",
                            "inventory_folder",
                            "secure_session_id",
                            "session_id",
                            "service_session_id",
                            "client_ip",
                            "viewer",
                            "channel",
                            "mac",
                            "id0",
                            "teleport_flags",
                            "start_pos",
                            "far",
                            "appearance_serial"
                        }

                        )
                            map[k] = value;
                    else
                        map[key] = value;
                    c.UnpackAgentCircuitData(map);
                }
                else if (op == "seeds")
                {
                    var seeds = new OSDArray();
                    for (int i = 1; i + 1 < p.Length; i += 2)
                    {
                        var pair = new OSDMap();
                        if (p[i] != "-")
                            pair["handle"] = OSD.FromString(Unhex(p[i]));
                        if (p[i + 1] != "-")
                            pair["seed"] = OSD.FromString(Unhex(p[i + 1]));
                        seeds.Add(pair);
                    }

                    c.UnpackAgentCircuitData(new OSDMap { { "children_seeds", seeds } });
                }
                else if (op == "seed-edit")
                {
                    ulong key = ulong.Parse(p[1]);
                    c.ChildrenCapSeeds ??= new Dictionary<ulong, string>();
                    if (p[2] == "1")
                        c.ChildrenCapSeeds.Remove(key);
                    else
                        c.ChildrenCapSeeds[key] = "changed";
                }
                else if (op == "urls")
                {
                    int mode = int.Parse(p[1]);
                    var map = new OSDMap();
                    var old = new OSDArray();
                    var modern = new OSDMap();
                    for (int i = 2; i + 1 < p.Length; i += 2)
                    {
                        var k = Unhex(p[i]);
                        var v = Unhex(p[i + 1]);
                        old.Add(OSD.FromString(k));
                        old.Add(OSD.FromString(v));
                        modern[k] = OSD.FromString(v);
                    }

                    if (mode != 1)
                        map["service_urls"] = old;
                    if (mode != 0)
                        map["serviceurls"] = modern;
                    c.UnpackAgentCircuitData(map);
                }
                else if (op == "url-edit")
                {
                    c.ServiceURLs ??= new Dictionary<string, object>();
                    if (p[2] == "1")
                        c.ServiceURLs.Remove(p[1]);
                    else
                        c.ServiceURLs[p[1]] = "changed";
                }
                else if (op == "objects")
                    c.ServiceURLs = new Dictionary<string, object>
                    {
                        {
                            "null",
                            null
                        },
                        {
                            "boolean",
                            true
                        },
                        {
                            "integer",
                            -7
                        },
                        {
                            "long",
                            -9223372036854775807L
                        },
                        {
                            "uuid",
                            UUID.Zero
                        },
                        {
                            "float",
                            .5f
                        },
                        {
                            "double",
                            1e22
                        },
                        {
                            "custom",
                            new Custom()
                        },
                        {
                            "floatinf",
                            float.PositiveInfinity
                        },
                        {
                            "doubleinf",
                            double.NegativeInfinity
                        }
                    };
                else if (op == "counter-object")
                    c.ServiceURLs = new Dictionary<string, object>
                    {
                        {
                            "counter",
                            new Counter()
                        }
                    };
                else if (op == "mutating-object")
                {
                    c.ServiceURLs = new Dictionary<string, object>();
                    c.ServiceURLs["first"] = new Mutating
                    {
                        urls = c.ServiceURLs
                    };
                }
                else if (op == "switching-object")
                {
                    c.ServiceURLs = new Dictionary<string, object>();
                    c.ServiceURLs["first"] = new Switching
                    {
                        change = () =>
                        {
                            c.ServiceURLs = new Dictionary<string, object>
                            {
                                {
                                    "new",
                                    "value"
                                }
                            };
                        }
                    };
                }
                else if (op == "appearance")
                {
                    c.Appearance = new AvatarAppearance
                    {
                        Serial = 17,
                        AvatarPreferencesHoverZ = .25f
                    };
                    c.Appearance.SetWearable(16, new AvatarWearable(new UUID("00000000-0000-0000-0000-000000000003"), new UUID("00000000-0000-0000-0000-000000000004")));
                }
                else if (op == "pack")
                    output = Canonical(c.PackAgentCircuitData(new EntityTransferContext { OutboundVersion = BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[1], 16)) }));
                else if (op == "roundtrip")
                    c.UnpackAgentCircuitData((OSDMap)OSDParser.DeserializeLLSDXml(OSDParser.SerializeLLSDXmlString(c.PackAgentCircuitData(null))));
                else if (op == "xml-order")
                {
                    var map = OSDParser.DeserializeLLSDXml("<llsd><map><key>serviceurls</key><map><key>z</key><string>Z</string><key>a</key><string>A</string><key>m</key><string>M</string></map></map></llsd>");
                    c.UnpackAgentCircuitData((OSDMap)OSDParser.DeserializeLLSDXml(OSDParser.SerializeLLSDXmlString(map)));
                }
            }
            catch (Exception e)
            {
                status = "EX";
                if (Environment.GetEnvironmentVariable("CIRCUIT_FIXTURE_DEBUG") == "1")
                    Console.Error.WriteLine(op + ": " + e);
            }

            string state;
            try
            {
                state = Canonical(c.PackAgentCircuitData(null)) + "|" + c.teleportFlags + "|" + (c.ChildrenCapSeeds != null ? "1" : "0") + (c.ServiceURLs != null ? "1" : "0");
            }
            catch
            {
                state = "STATEEX";
            }

            Console.WriteLine(status + "|" + output + "|" + state);
        }
    }
}

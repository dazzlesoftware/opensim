using System;
using System.Collections.Generic;
using System.Text;
using OpenMetaverse.StructuredData;
class OsdJsonReference
{
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Types(OSD v)
    {
        string s = v.Type switch { OSDType.Unknown => "u",
                                   OSDType.String => "s",
                                   OSDType.Integer => "i",
                                   OSDType.Boolean => "b",
                                   OSDType.Real => "r",
                                   OSDType.UUID => "id",
                                   OSDType.Date => "d",
                                   OSDType.URI => "uri",
                                   OSDType.Binary => "bin",
                                   OSDType.Array => "a",
                                   OSDType.Map => "m",
                                   OSDType.Long => "l",
                                   _ => "?" };
        if (v is OSDArray a)
            foreach (var x in a)
                s += ":" + Types(x);
        if (v is OSDMap m)
            foreach (KeyValuePair<string, OSD> p in m)
                s += ":" + H(p.Key) + "=" + Types(p.Value);
        return s;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                OSD v;
                if (line.StartsWith("p "))
                    v = OSDParser.DeserializeJson(
                        Encoding.UTF8.GetString(Convert.FromHexString(line.Substring(2))));
                else
                {
                    OSD scalar = FixtureValue.Build(line.Substring(2));
                    v = scalar;
                    if (line.StartsWith("m "))
                    {
                        v = new OSDMap { { "field", scalar },
                                         { "nested", new OSDMap { { "field", scalar } } },
                                         { "array",
                                           new OSDArray { scalar, new OSDMap { { "field", scalar } } } } };
                    }
                }
                Console.WriteLine(H(OSDParser.SerializeJsonString(v, false)) + "|" +
                                  H(OSDParser.SerializeJsonString(v, true)) + "|" + Types(v));
            }
            catch
            {
                Console.WriteLine("EX");
            }
        }
    }
}

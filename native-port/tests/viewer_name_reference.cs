using System;
using System.IO;
using System.Text;
using System.Globalization;
using System.Security.Cryptography;
using OpenSim.Framework;
using OpenMetaverse.StructuredData;
public partial class ViewerReference
{
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Units(string s)
    {
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static string FromUnits(string s)
    {
        var b = new StringBuilder();
        for (int i = 0; i < s.Length; i += 4)
            b.Append((char)Convert.ToInt32(s.Substring(i, 4), 16));
        return b.ToString();
    }
    static void Profile()
    {
        Console.WriteLine("runtime:" + Environment.Version);
        var bits = new byte[8192];
        for (int i = 0; i < 65536; i++)
            if (Char.IsLetter((char)i))
                bits[i / 8] |= (byte)(1 << (i % 8));
        Console.WriteLine("letters:" + Convert.ToHexString(bits).ToLowerInvariant());
        for (int i = 0; i <= 0x10ffff; i++)
        {
            string value = i < 0x10000 ? new string((char)i, 1) : Char.ConvertFromUtf32(i);
            string lower = value.ToLower();
            if (value != lower)
                Console.WriteLine("case:" + i.ToString("x") + ":" + Units(lower));
        }
    }
    static void AllViewers()
    {
        var agent = new AgentCircuitData { Channel = "Channel" };
        using var stream = new MemoryStream();
        for (int i = 0; i <= 0x10ffff; i++)
        {
            agent.Viewer = (i < 0x10000 ? new string((char)i, 1) : Char.ConvertFromUtf32(i)) + " 1";
            byte[] bytes = Encoding.UTF8.GetBytes(GetViewerName(agent));
            stream.Write(BitConverter.GetBytes(bytes.Length));
            stream.Write(bytes);
        }
        Console.WriteLine("all:" + stream.Length + ":" +
                          Convert.ToHexString(MD5.HashData(stream.ToArray())).ToLowerInvariant());
    }
    static void Main(string[] args)
    {
        Culture.SetCurrentCulture();
        if (args.Length > 0 && args[0] == "profile")
        {
            Profile();
            return;
        }
        if (args.Length > 0 && args[0] == "all")
        {
            AllViewers();
            return;
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                if (line.StartsWith("L "))
                {
                    Console.WriteLine(Units(FromUnits(line.Substring(2)).ToLower()));
                    continue;
                }
                var map =
                    (OSDMap)OSDParser.DeserializeJson(Encoding.UTF8.GetString(Convert.FromHexString(line)));
                string Opt(string key) => map[key].Type == OSDType.Unknown ? null : map[key].AsString();
                var a = new AgentCircuitData { Viewer = Opt("viewer"), Channel = Opt("channel") };
                Console.WriteLine(H(GetViewerName(map["null_agent"].AsBoolean() ? null : a)));
            }
            catch
            {
                Console.WriteLine("EX");
            }
        }
    }
}

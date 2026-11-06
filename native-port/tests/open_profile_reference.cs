using System;
using System.Collections;
using System.Globalization;
using System.Text;
using OpenMetaverse;
using OpenSim.Framework;
class ProbeBox
{
    public override string ToString()
    {
        Host.Trace += "text;";
        if ((Host.Flags & 32) != 0)
            Host.Props = Host.Seed(5);
        if ((Host.Flags & 64) != 0)
            Host.Props = null;
        if ((Host.Flags & 16) != 0)
            throw new Exception("stringify");
        return Host.Payload;
    }
}
static class XMLRPCRequester
{
    public static Hashtable SendRequest(Hashtable data, string method, string uri)
    {
        Host.Trace += "rpc:" + data["avatar_id"] + ":" + Host.Raw(method) + ":" + Host.Raw(uri) + ";";
        if ((Host.Flags & 2) != 0)
            throw new Exception("request");
        if ((Host.Flags & 4) != 0)
            Host.Props = Host.Seed(5);
        if ((Host.Flags & 8) != 0)
            Host.Props = null;
        return Host.Response;
    }
}
static class Host
{
    public static string Trace, Payload;
    public static int Flags;
    public static Hashtable Response;
    public static UserProfileProperties Props;
    public static string H(string s)
    {
        return s == null       ? "-"
               : s.Length == 0 ? "."
                               : Convert.ToHexString(Encoding.Unicode.GetBytes(s)).ToLowerInvariant();
    }
    // Preserve raw UTF16, including lone surrogates.
    public static string Raw(string s)
    {
        if (s == null)
            return "-";
        if (s.Length == 0)
            return ".";
        string v = "";
        foreach (char c in s)
            v += ((int)c & 255).ToString("x2") + ((int)c >> 8).ToString("x2");
        return v;
    }
    public static string D(string h)
    {
        if (h == "-")
            return null;
        if (h == ".")
            return "";
        string s = "";
        for (int i = 0; i < h.Length; i += 4)
            s += (char)(Convert.ToInt32(h.Substring(i, 2), 16) |
                        (Convert.ToInt32(h.Substring(i + 2, 2), 16) << 8));
        return s;
    }
    public static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    public static UserProfileProperties Seed(int n) => new UserProfileProperties {
        UserId = Id(n),
        PartnerId = Id(2),
        ImageId = Id(3),
        FirstLifeImageId = Id(4),
        WebUrl = "seed url",
        AboutText = "seed about",
        FirstLifeText = "seed life",
        WantToMask = 47,
        SkillsMask = -31,
        WantToText = "seed wants",
        SkillsText = "seed skills",
        Language = "seed language",
        PublishProfile = true,
        PublishMature = true
    };
    static object Atom(int kind) => kind switch {
        0 => null,
        1 => Payload,
        2 => int.Parse(Payload),
        3 => bool.Parse(Payload),
        4 => double.Parse(Payload, CultureInfo.InvariantCulture),
        5 => long.Parse(Payload),
        6 => new ArrayList(),
        7 => new Hashtable(),
        8 => new byte[] { 1, 2 },
        9 => new DateTime(long.Parse(Payload)),
        10 => new ProbeBox(),
        _ => throw new Exception("kind")
    };
    public static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        string[] keys = { "ProfileUrl",     "AboutText",  "FirstLifeAboutText", "Image",
                          "FirstLifeImage", "Partner",    "wantmask",           "wanttext",
                          "skillsmask",     "skillstext", "languages" };
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            int mode = int.Parse(a[0]), field = int.Parse(a[2]), kind = int.Parse(a[3]);
            Flags = int.Parse(a[1]);
            Payload = D(a[4]);
            string uri = D(a[5]);
            Trace = "";
            Props = (Flags & 1) != 0 ? null : Seed(1);
            var initial = Props;
            object atom = Atom(kind);
            var row = new Hashtable { { "ProfileUrl", "url" },
                                      { "AboutText", "about" },
                                      { "FirstLifeAboutText", "life" },
                                      { "Image", Id(6).ToString() },
                                      { "FirstLifeImage", Id(7).ToString() },
                                      { "Partner", Id(8).ToString() },
                                      { "wantmask", "-2147483648" },
                                      { "wanttext", "wants" },
                                      { "skillsmask", "2147483647" },
                                      { "skillstext", "skills" },
                                      { "languages", "en" } };
            if (field >= 0)
                row[keys[field]] = atom;
            if (field == -2)
                row.Clear();
            Response = mode switch {
                0 => null,
                1 => new Hashtable(),
                2 => new Hashtable { { "data", null } },
                3 => new Hashtable { { "data", new ArrayList() } },
                4 => new Hashtable { { "data", new ArrayList { null } } },
                5 => new Hashtable { { "data", atom } },
                6 => new Hashtable { { "data", new ArrayList { atom } } },
                _ => new Hashtable { { "data", new ArrayList { row } } }
            };
            string result;
            try
            {
                result = "B:" + (new OpenSim.Services.UserProfilesService.OpenProfileClient(uri)
                                         .RequestAvatarPropertiesUsingOpenProfile(ref Props)
                                     ? "1"
                                     : "0");
            }
            catch (Exception e)
            {
                result = "E:" + Raw(e.Message);
            }
            Console.WriteLine(result + "|" + Trace + "|" +
                              (ReferenceEquals(initial, Props) ? "same" : "new") + "|" + Dump(Props) + "|" +
                              Dump(initial));
        }
    }
    static string Dump(UserProfileProperties p) => p == null
                                                       ? "N"
                                                       : p.UserId + ":" + p.PartnerId + ":" + p.ImageId +
                                                             ":" + p.FirstLifeImageId + ":" +
                                                             Raw(p.WebUrl) + ":" + Raw(p.AboutText) + ":" +
                                                             Raw(p.FirstLifeText) + ":" + p.WantToMask + ":" +
                                                             Raw(p.WantToText) + ":" + p.SkillsMask + ":" +
                                                             Raw(p.SkillsText) + ":" + Raw(p.Language) +
                                                             ":" + p.PublishProfile + ":" + p.PublishMature;
}

namespace OpenSim.Framework
{
    public class GroupMembershipData
    {
    }
    public interface IClientAPI
    {
    }
}

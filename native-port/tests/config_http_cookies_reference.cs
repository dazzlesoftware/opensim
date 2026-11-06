using System;
using System.Net;
using System.Collections;
using System.Reflection;
using System.Text;
using System.Globalization;
class Hash : IEqualityComparer
{
    public new bool Equals(object a, object b) => string.Equals((string)a, (string)b,
                                                                StringComparison.Ordinal);
    public int GetHashCode(object a)
    {
        uint h = 2166136261;
        foreach (char c in (string)a)
            h = unchecked((h ^ c) * 16777619);
        return unchecked((int)h);
    }
}
class Program
{
    static string D(string s)
    {
        if (s == "~")
            return null;
        if (s == "-")
            return "";
        var b = new StringBuilder();
        for (int i = 0; i < s.Length; i += 4)
            b.Append((char)Convert.ToInt32(s.Substring(i, 4), 16));
        return b.ToString();
    }
    static string H(string s)
    {
        if (s == null)
            return "~";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.Length == 0 ? "-" : b.ToString();
    }
    static string Error(Exception e) => e.GetType().FullName + ":" +
                                        unchecked((uint)e.HResult).ToString("x8") + ":" + H(e.Message) + ":" +
                                        (e is ArgumentException a ? H(a.ParamName) : "~") + ":" +
                                        (e.InnerException == null ? "~" : Error(e.InnerException));
    static string Snapshot(CookieCollection cookies)
    {
        var b = new StringBuilder();
        foreach (Cookie c in cookies)
        {
            var variant = typeof(Cookie)
                              .GetField("m_cookieVariant", BindingFlags.NonPublic | BindingFlags.Instance)
                              .GetValue(c);
            b.Append(H(c.Name) + "," + H(c.Value) + "," + H(c.Domain) + "," + H(c.Path) + "," + H(c.Port) +
                     "," + c.Version + "," + Convert.ToInt32(variant) + "," + (c.Secure ? 1 : 0) + "," +
                     (c.HttpOnly ? 1 : 0) + "," + (c.Discard ? 1 : 0) + ";");
        }
        return b.ToString();
    }
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--fqdn")
        {
            var jar = new CookieContainer();
            Console.WriteLine(
                H((string) typeof(CookieContainer)
                      .GetField("m_fqdnMyDomain", BindingFlags.NonPublic | BindingFlags.Instance)
                      .GetValue(jar)));
            return;
        }
        bool nativeHash = args.Length > 0 && args[0] == "--native-hash";
        if (args.Length > 0 && (args[0] == "--hash" || nativeHash))
        {
            var seed = (ulong) typeof(string)
                           .Assembly.GetType("System.Marvin")
                           .GetProperty("DefaultSeed",
                                        BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static)
                           .GetValue(null);
            Console.WriteLine("SEED " + seed.ToString("x16"));
            if (args[0] == "--hash")
            {
                string row;
                while ((row = Console.ReadLine()) != null)
                    Console.WriteLine(unchecked((uint)D(row).GetHashCode()).ToString("x8"));
                return;
            }
        }
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            CookieContainer jar = null;
            var result = new System.Collections.Generic.List<string>();
            foreach (var operation in line.Split('|'))
            {
                var a = operation.Split(' ');
                try
                {
                    switch (a[0])
                    {
                    case "N":
                        jar = new CookieContainer(int.Parse(a[1]), int.Parse(a[2]), int.Parse(a[3]));
                        typeof(CookieContainer)
                            .GetField("m_fqdnMyDomain", BindingFlags.NonPublic | BindingFlags.Instance)
                            .SetValue(jar, D(a[4]));
                        if (!nativeHash)
                            typeof(CookieContainer)
                                .GetField("m_domainTable", BindingFlags.NonPublic | BindingFlags.Instance)
                                .SetValue(jar, new Hashtable(new Hash()));
                        break;
                    case "S":
                        jar.SetCookies(a[1] == "~" ? null : new Uri(D(a[1])), D(a[2]));
                        break;
                    case "RX":
                        try
                        {
                            jar.SetCookies(new Uri(D(a[1])), D(a[2]));
                        }
                        catch (CookieException)
                        {
                        }
                        break;
                    case "H":
                        result.Add(H(jar.GetCookieHeader(a[1] == "~" ? null : new Uri(D(a[1])))));
                        break;
                    case "G":
                        result.Add(Snapshot(jar.GetCookies(new Uri(D(a[1])))));
                        break;
                    case "C":
                        result.Add(jar.Count.ToString());
                        break;
                    }
                    result.Add("ok");
                }
                catch (Exception e)
                {
                    result.Add(Error(e));
                }
            }
            Console.WriteLine(string.Join("/", result));
        }
    }
}

using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Globalization;
using OpenSim.Framework;
public class CacheFixtureTimer : IDisposable
{
    public static CacheFixtureTimer Current;
    TimerCallback callback;
    object state;
    bool disposed;
    public CacheFixtureTimer(TimerCallback cb, object s, int due, int period)
    {
        callback = cb;
        state = s;
        Current = this;
    }
    public void Dispose()
    {
        disposed = true;
    }
    public bool Change(int due, int period) => true;
    public static void Fire()
    {
        if (Current != null && !Current.disposed)
            Current.callback(Current.state);
    }
}
public static class DnsFixture
{
    public static bool SystemDns;
    public static double Now;
    public static int clocks;
    public static string trace = "";
    public static double Clock()
    {
        ++clocks;
        return Now;
    }
    public static IPHostEntry Lookup(string name)
    {
        trace += name + ";";
        if (SystemDns)
            return Dns.GetHostEntry(name);
        IPAddress I(string s) => IPAddress.Parse(s);
        if (name == "throw")
            throw new SocketException();
        if (name == "null")
            return null;
        return new IPHostEntry { AddressList = name switch {
            "empty" => Array.Empty<IPAddress>(), "badlist" => new[] { null, I("127.0.0.1") },
            "later-null" => new[] { I("::1"), null, I("127.0.0.1") },
            "first-v4-null" => new[] { I("127.0.0.1"), null }, "v6" => new[] { I("2001:db8::1"), I("::1") },
            "mixed" => new[] { I("2001:db8::1"), I("203.0.113.5"), I("198.51.100.2") },
            "any" => new[] { I("0.0.0.0") },
            _ => new[] { I("198.51.100.7") }
        } };
    }
}
class HostResolverReference
{
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Text(string s) => s == "-"   ? null
                                    : s == "." ? ""
                                               : Encoding.UTF8.GetString(Convert.FromHexString(s));
    static string Address(IPAddress a) =>
        a == null ? "null"
                  : (a.AddressFamily == AddressFamily.InterNetworkV6 ? "6:" : "4:") + H(a.ToString()) +
                        ":" + Convert.ToHexString(a.GetAddressBytes()).ToLowerInvariant() + ":" +
                        (a.AddressFamily == AddressFamily.InterNetworkV6 ? a.ScopeId : a.Address) + ":" +
                        (a.Equals(IPAddress.Any) || a.Equals(IPAddress.IPv6Any) ? "1" : "0");
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "assembly")
        {
            Console.WriteLine(
                System.Text.Json.JsonSerializer.Serialize(new { path = typeof(IPAddress).Assembly.Location,
                                                                runtime = Environment.Version.ToString() }));
            return;
        }
        DnsFixture.SystemDns = args.Length > 0;
        _ = Util.CacheCount;
        IPAddress last = null;
        IPEndPoint endpoint = null;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            string result = "OK";
            try
            {
                if (p[0] == "p")
                {
                    Console.WriteLine(IPAddress.TryParse(Text(p[1]), out var parsed) ? Address(parsed)
                                                                                     : "null");
                    continue;
                }
                switch (p[0])
                {
                case "reset":
                    DnsFixture.Now = 0;
                    DnsFixture.clocks = 0;
                    DnsFixture.trace = "";
                    last = null;
                    endpoint = null;
                    Util.Reset();
                    break;
                case "h":
                    last = Util.GetHostFromDNS(Text(p[1]));
                    break;
                case "u":
                    last = Util.GetHostFromURL(Text(p[1]));
                    break;
                case "e":
                    endpoint = Util.getEndPoint(Text(p[1]), int.Parse(p[2]));
                    if (endpoint != null)
                        last = endpoint.Address;
                    break;
                case "d":
                    IPAddress.TryParse(Text(p[1]), out var address);
                    endpoint = Util.getEndPoint(address, int.Parse(p[2]));
                    if (endpoint != null)
                        last = endpoint.Address;
                    break;
                case "scope":
                    last.ScopeId = long.Parse(p[1]);
                    break;
                case "v4":
                    last.Address = long.Parse(p[1]);
                    break;
                case "time":
                    DnsFixture.Now = double.Parse(p[1], CultureInfo.InvariantCulture);
                    break;
                case "purge":
                    CacheFixtureTimer.Fire();
                    break;
                case "clear":
                    Util.Clear();
                    break;
                }
            }
            catch
            {
                result = "EX";
            }
            Console.WriteLine(result + "|" + DnsFixture.clocks + "|" + Util.CacheCount + "|" +
                              H(DnsFixture.trace) + "|" + Address(last) + "|" +
                              (endpoint == null ? "null" : H(endpoint.ToString())));
        }
    }
}

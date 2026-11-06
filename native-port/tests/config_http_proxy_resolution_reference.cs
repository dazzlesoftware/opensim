using System;
using System.Net;
using System.Net.Http;
using System.IO;
using System.Threading.Tasks;
class Stop : Exception
{
}
class Proxy : IWebProxy
{
    public string Mode = "";
    public Uri Value = null!;
    public int Bypass, Get;
    public ICredentials? Credentials { get; set; }
    public bool IsBypassed(Uri target)
    {
        Bypass++;
        if (Mode == "bthrow")
            throw new Exception("bypass");
        return Mode == "bypass";
    }
    public Uri? GetProxy(Uri target)
    {
        Get++;
        if (Mode == "gthrow")
            throw new Exception("get");
        return Mode == "null" ? null : Value;
    }
}
class Reference
{
    static string Hex(string s) =>
        Convert.ToHexString(System.Text.Encoding.BigEndianUnicode.GetBytes(s)).ToLowerInvariant();
    static string Decode(string s) =>
        System.Text.Encoding.BigEndianUnicode.GetString(Convert.FromHexString(s));
    static void Main()
    {
        string? row;
        while ((row = Console.ReadLine()) != null)
        {
            var parts = row.Split(' ');
            var p = new Proxy { Mode = parts[0] };
            DnsEndPoint? endpoint = null;
            try
            {
                p.Value = new Uri(Decode(parts[1]), UriKind.RelativeOrAbsolute);
                using var handler = new SocketsHttpHandler { Proxy = p, UseProxy = p.Mode != "none" };
                handler.ConnectCallback = (context, cancellation) =>
                {
                    endpoint = context.DnsEndPoint;
                    return ValueTask.FromException<Stream>(new Stop());
                };
                using var client = new HttpClient(handler);
                try
                {
                    client.GetStreamAsync("http://destination.example:8123/").GetAwaiter().GetResult();
                }
                catch when (endpoint != null)
                {
                }
                if (endpoint == null)
                    throw new Exception("Missing connect callback");
                Console.Write("OK:" + Hex(endpoint.Host) + ":" + endpoint.Port);
            }
            catch (Exception e)
            {
                Console.Write("E:" + e.GetType().FullName + ":" + e.HResult + ":" + Hex(e.Message));
            }
            Console.WriteLine(":" + p.Bypass + ":" + p.Get);
        }
    }
}

using System;
using System.Net;
using System.Net.Http;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Collections.Generic;
static partial class WebUtil
{
    public static SocketsHttpHandler SharedSocketsHttpHandlerNoRedir, SharedSocketsHttpHandler;
}
class Program
{
    static string H(string text)
    {
        if (text.Length == 0)
            return "-";
        var b = new StringBuilder();
        foreach (char c in text)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static void Main()
    {
        var proxies = new Dictionary<int, WebProxy>();
        var identities = new List<SocketsHttpHandler>();
        var clients = new Dictionary<int, (HttpClient, SocketsHttpHandler)>();
        int next = 0;
        int Id(SocketsHttpHandler h)
        {
            if (h == null)
                return 0;
            int at = identities.IndexOf(h);
            if (at < 0)
            {
                identities.Add(h);
                return identities.Count;
            }
            return at + 1;
        }
        string Snapshot(SocketsHttpHandler h)
        {
            if (h == null)
                return "-";
            int proxy = 0;
            foreach (var p in proxies)
                if (ReferenceEquals(p.Value, h.Proxy))
                    proxy = p.Key;
            return string.Join("/", Id(h), h.AllowAutoRedirect ? 1 : 0, h.MaxAutomaticRedirections,
                               (int)h.AutomaticDecompression, (long)h.ConnectTimeout.TotalMilliseconds,
                               h.PreAuthenticate ? 1 : 0, h.UseCookies ? 1 : 0, h.MaxConnectionsPerServer,
                               (long)h.PooledConnectionIdleTimeout.TotalMilliseconds,
                               (long)h.PooledConnectionLifetime.TotalMilliseconds,
                               (int)h.SslOptions.EnabledSslProtocols,
                               (int)h.SslOptions.CertificateRevocationCheckMode, h.UseProxy ? 1 : 0, proxy);
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            string output = "";
            try
            {
                switch (a[0])
                {
                case "S": {
                    int flags = int.Parse(a[1]), proxy = int.Parse(a[2]);
                    if (proxy != 0 && !proxies.ContainsKey(proxy))
                        proxies[proxy] = new WebProxy("http://127.0.0.1:" + (10000 + proxy));
                    WebUtil.SetupHTTPClients((flags & 1) != 0, (flags & 2) != 0,
                                             proxy == 0 ? null : proxies[proxy], int.Parse(a[3]));
                    output = "OK";
                    break;
                }
                case "G": {
                    bool no = a[1] == "1";
                    var c = no ? WebUtil.GetGlobalNoRedirHttpClient(int.Parse(a[2]), int.Parse(a[3]))
                               : WebUtil.GetNewGlobalHttpClient(int.Parse(a[2]), int.Parse(a[3]));
                    var h = no ? WebUtil.SharedSocketsHttpHandlerNoRedir : WebUtil.SharedSocketsHttpHandler;
                    clients[++next] = (c, h);
                    output =
                        $"G:{next}:{Id(h)}:{(long)c.Timeout.TotalMilliseconds}:{c.MaxResponseContentBufferSize}:{(c.DefaultRequestHeaders.ExpectContinue==false?0:1)}";
                    break;
                }
                case "D": {
                    int id = int.Parse(a[1]);
                    clients[id].Item1.Dispose();
                    output = "D:" + id;
                    break;
                }
                case "U": {
                    var request = new HttpRequestMessage(HttpMethod.Get, "http://127.0.0.1:9/");
                    clients[int.Parse(a[1])].Item1.Send(request, HttpCompletionOption.ResponseHeadersRead);
                    output = "UNEXPECTED_SEND";
                    break;
                }
                case "P": {
                    var errors = (SslPolicyErrors) int.Parse(a[1]);
                    string Test(SocketsHttpHandler h) =>
                        h == null                                                                    ? "-"
                        : h.SslOptions.RemoteCertificateValidationCallback(null, null, null, errors) ? "1"
                                                                                                     : "0";
                    output = "P:" + Test(WebUtil.SharedSocketsHttpHandler) + ":" +
                             Test(WebUtil.SharedSocketsHttpHandlerNoRedir);
                    break;
                }
                }
            }
            catch (Exception e)
            {
                output = "E:" + e.GetType().FullName + ":" + e.HResult.ToString("x") + ":" + H(e.Message);
            }
            Console.WriteLine(output + "|" + Snapshot(WebUtil.SharedSocketsHttpHandlerNoRedir) + "|" +
                              Snapshot(WebUtil.SharedSocketsHttpHandler));
        }
        foreach (var c in clients.Values)
            c.Item1.Dispose();
    }
}

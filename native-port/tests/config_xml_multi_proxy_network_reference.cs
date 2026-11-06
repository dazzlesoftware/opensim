using System;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Linq;
class Reference
{
    static List<int> Trace = new();
    static string Hex(string s) =>
        Convert.ToHexString(System.Text.Encoding.BigEndianUnicode.GetBytes(s)).ToLowerInvariant();
    static string Error(Exception e)
    {
        int code = e is HttpRequestException h ? (int)h.HttpRequestError
                   : e is HttpIOException io   ? (int)io.HttpRequestError
                   : e is SocketException s    ? (int)s.SocketErrorCode
                                               : -1;
        return e.GetType().FullName + ":" + unchecked((uint)e.HResult).ToString("x") + ":" + code + ":" +
               Hex(e.Message) + (e.InnerException == null ? "" : "|" + Error(e.InnerException));
    }
    static async ValueTask<Stream> Connect(SocketsHttpConnectionContext context, CancellationToken token)
    {
        lock (Trace) Trace.Add(context.DnsEndPoint.Port);
        if (context.DnsEndPoint.Host != "127.0.0.1")
            throw new InvalidOperationException("Fixture expected a loopback proxy endpoint");
        var socket = new Socket(SocketType.Stream, ProtocolType.Tcp);
        try
        {
            await socket.ConnectAsync(context.DnsEndPoint, token);
            return new NetworkStream(socket, true);
        }
        catch
        {
            socket.Dispose();
            throw;
        }
    }
    static void Main(string[] args)
    {
        var assembly = typeof(HttpClient).Assembly;
        IWebProxy proxy;
        if (args[0].StartsWith("env:", StringComparison.Ordinal))
        {
            foreach (var name in new[] { "http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY",
                                         "all_proxy", "ALL_PROXY", "no_proxy", "NO_PROXY",
                                         "GATEWAY_INTERFACE" })
                Environment.SetEnvironmentVariable(name, null);
            Environment.SetEnvironmentVariable("http_proxy", args[0].Substring(4));
            var values = new object?[] { null };
            if (!(bool)assembly.GetType("System.Net.Http.HttpEnvironmentProxy", true)!
                     .GetMethod("TryCreate", BindingFlags.Public | BindingFlags.NonPublic |
                                                 BindingFlags.Static)!.Invoke(null, values)!)
                throw new Exception("Missing environment proxy");
            proxy = (IWebProxy)values[0]!;
        }
        else
        {
            var helperType = assembly.GetType("System.Net.Http.WinInetProxyHelper", true)!;
            var helper = RuntimeHelpers.GetUninitializedObject(helperType);
            helperType.GetField("_proxy", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(helper,
                                                                                                    args[0]);
            var proxyType = assembly.GetType("System.Net.Http.HttpWindowsProxy", true)!;
            var constructor =
                proxyType.GetConstructors(BindingFlags.NonPublic | BindingFlags.Instance).Single();
            proxy = (IWebProxy)constructor.Invoke(new object?[] { helper, null });
        }

        using var handler = new SocketsHttpHandler { Proxy = proxy, ConnectCallback = Connect };
        using var client = new HttpClient(handler);
        for (int request = 0; request < 2; request++)
        {
            try
            {
                using var stream =
                    client.GetStreamAsync("http://target.example:8123" + args[1]).GetAwaiter().GetResult();
                using var memory = new MemoryStream();
                stream.CopyTo(memory);
                Console.WriteLine("OK:" + Convert.ToHexString(memory.ToArray()).ToLowerInvariant());
            }
            catch (Exception e)
            {
                Console.WriteLine("E:" + Error(e));
            }
        }
        Console.WriteLine("TRACE:" + string.Concat(Trace.Select(p => p + ",")));
        var cache = proxy.GetType()
                        .GetField("_failedProxies", BindingFlags.NonPublic | BindingFlags.Instance)
                        ?.GetValue(proxy);
        var entries = cache == null
                          ? new Dictionary<Uri, long>()
                          : (IDictionary<Uri, long>)cache.GetType()
                                .GetField("_failedProxies",
                                          BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(cache)!;
        Console.WriteLine(
            "FAILED:" + string.Concat(entries.Keys.Select(u => u.Port).OrderBy(p => p).Select(p => p + ",")));
    }
}

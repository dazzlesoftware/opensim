using System;
using System.Net.Http;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
class Program
{
    static string D(string s)
    {
        var b = new StringBuilder();
        for (int i = 0; i < s.Length; i += 4)
            b.Append((char)Convert.ToInt32(s.Substring(i, 4), 16));
        return b.ToString();
    }
    static string H(string s)
    {
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static void Main(string[] args)
    {
        bool resolve = Array.IndexOf(args, "--resolve") >= 0;
        HttpClient.DefaultProxy = new System.Net.WebProxy();
        string? row;
        while ((row = Console.ReadLine()) != null)
        {
            var p = row.Split(' ');
            int port = int.Parse(p[1]);
            var u = new UriBuilder("http", D(p[0]), port).Uri;
            string host = u.HostNameType == UriHostNameType.IPv6 ? "[" + u.IdnHost + "]" : u.IdnHost;
            HttpRequestException request;
            SocketException socket;
            if (resolve)
            {
                // Invoke the actual consumed BCL entry point, with its real HttpClient path.
                var method = typeof(System.Xml.XmlReader)
                                 .Assembly.GetType("System.Xml.XmlDownloadManager", true)!.GetMethod(
                                     "GetStream", BindingFlags.NonPublic | BindingFlags.Static)!;
                try
                {
                    using var stream =
                        (System.IO.Stream)method.Invoke(null, new object?[] { u, null, null })!;
                    throw new InvalidOperationException("DNS fault fixture unexpectedly resolved a host");
                }
                catch (TargetInvocationException e) when (e.InnerException is HttpRequestException)
                {
                    request = (HttpRequestException)e.InnerException;
                    socket = (SocketException)request.InnerException!;
                }
            }
            else
            {
                int code =
                    p.Length < 3
                        ? 11001
                        : p[2] switch { "noname" => 11001,
                                        "again" => 11002,
                                        "fail" => 11003,
                                        "nodata" => OperatingSystem.IsWindows() ? 11004 : 11001,
                                        "family" => 10047,
                                        "badflags" => 10022,
                                        "addrfamily" or "service" or "memory" or "system" => -1,
                                        _ => throw new ArgumentException("unknown DNS fixture outcome") };
                socket = new SocketException(code);
                var method = typeof(HttpClient)
                                 .Assembly.GetType("System.Net.Http.ConnectHelper", true)!.GetMethod(
                                     "CreateWrappedException", BindingFlags.NonPublic | BindingFlags.Static)!;
                request = (HttpRequestException)method.Invoke(
                    null, new object[] { socket, host, port, System.Threading.CancellationToken.None })!;
            }
            var retry =
                typeof(HttpRequestException)
                    .GetProperty("AllowRetry", BindingFlags.Instance |
                                                   BindingFlags.NonPublic)!.GetValue(request)!.ToString();
            Console.WriteLine(request.GetType().FullName + ":" + request.HResult + ":" +
                              (int)request.HttpRequestError + ":" + retry + ":" + H(request.Message) + "|" +
                              socket.GetType().FullName + ":" + socket.HResult + ":" +
                              socket.NativeErrorCode + ":" + (int)socket.SocketErrorCode + ":" +
                              H(socket.Message));
        }
    }
}

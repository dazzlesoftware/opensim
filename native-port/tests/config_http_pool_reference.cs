using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
class Program
{
    static void Main(string[] args)
    {
        using var handler = new SocketsHttpHandler { UseProxy = false,
                                                     UseCookies = false,
                                                     AllowAutoRedirect = true,
                                                     MaxAutomaticRedirections = 10,
                                                     AutomaticDecompression = DecompressionMethods.None,
                                                     MaxConnectionsPerServer = int.Parse(args[1]),
                                                     PooledConnectionIdleTimeout =
                                                         TimeSpan.FromMilliseconds(long.Parse(args[2])),
                                                     PooledConnectionLifetime =
                                                         TimeSpan.FromMilliseconds(long.Parse(args[3])),
                                                     ConnectTimeout = TimeSpan.FromSeconds(120) };
        var streams = new Dictionary<string, (HttpClient, HttpResponseMessage, Stream)>();
        var mutex = new object();
        var jobs = new Dictionary<string, Task<string>>();
        string Open(string id, string path, string method, int timeout, string connection)
        {
            var client = new HttpClient(handler, false) { Timeout = TimeSpan.FromMilliseconds(timeout) };
            try
            {
                var request = new HttpRequestMessage(new HttpMethod(method),
                                                     path.StartsWith("http://") ? path : args[0] + path);
                if (connection == "c")
                    request.Headers.ConnectionClose = true;
                var response = client.Send(request, HttpCompletionOption.ResponseHeadersRead);
                var stream = response.Content.ReadAsStream();
                lock (mutex) streams[id] = (client, response, stream);
                return $"O {id} {(int)response.StatusCode} {string.Join("",response.Headers.GetValues("X-Connection-Id"))}";
            }
            catch (Exception e)
            {
                client.Dispose();
                return $"E {id} {e.GetType().FullName}";
            }
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            var id = a[1];
            switch (a[0])
            {
            case "O":
                Console.WriteLine(Open(id, a[2], a[3], int.Parse(a[4]), a[5]));
                break;
            case "A":
                jobs[id] = Task.Run(() => Open(id, a[2], a[3], int.Parse(a[4]), a[5]));
                break;
            case "J":
                Console.WriteLine(jobs[id].GetAwaiter().GetResult());
                jobs.Remove(id);
                break;
            case "S":
                Thread.Sleep(int.Parse(id));
                break;
            case "D": {
                (HttpClient, HttpResponseMessage, Stream) item;
                lock (mutex) item = streams[id];
                item.Item3.Dispose();
                item.Item2.Dispose();
                item.Item1.Dispose();
                Console.WriteLine("D " + id);
                break;
            }
            case "C": {
                (HttpClient, HttpResponseMessage, Stream) item;
                lock (mutex)
                {
                    item = streams[id];
                    streams.Remove(id);
                }
                item.Item3.Dispose();
                item.Item2.Dispose();
                item.Item1.Dispose();
                Console.WriteLine("C " + id);
                break;
            }
            case "R": {
                Stream stream;
                lock (mutex) stream = streams[id].Item3;
                int count = int.Parse(a[2]);
                using var output = new MemoryStream();
                var buffer = new byte[4096];
                try
                {
                    while (count < 0 || output.Length < count)
                    {
                        int n = stream.Read(buffer, 0,
                                            count < 0 ? buffer.Length
                                                      : Math.Min(buffer.Length, count - (int)output.Length));
                        if (n == 0)
                            break;
                        output.Write(buffer, 0, n);
                    }
                    Console.WriteLine("R " + id + " " +
                                      (output.Length == 0
                                           ? "-"
                                           : Convert.ToHexString(output.ToArray()).ToLowerInvariant()));
                }
                catch (Exception e)
                {
                    Console.WriteLine("E " + id + " " + e.GetType().FullName);
                }
                break;
            }
            }
        }
        foreach (var item in streams.Values)
        {
            item.Item3.Dispose();
            item.Item2.Dispose();
            item.Item1.Dispose();
        }
    }
}

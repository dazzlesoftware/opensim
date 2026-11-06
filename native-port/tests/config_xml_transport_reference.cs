using System;
using System.IO;
using System.Net;
using System.Threading.Tasks;
using System.Collections.Generic;
static class State
{
    public static int Mask;
    public static List<string> Trace = new();
    public static void Step(int bit, string name)
    {
        Trace.Add(name);
        if ((Mask & (1 << bit)) != 0)
            throw new InvalidOperationException(name);
    }
}
namespace System.Net.Http
{
    class HttpClientHandler
    {
        public HttpClientHandler()
        {
            State.Step(0, "handler");
        }
        public ICredentials Credentials
        {
            set {
                State.Step(2, "credentials");
            }
        }
        public IWebProxy Proxy
        {
            set {
                State.Step(3, "proxy");
            }
        }
    }
    class HttpClient : IDisposable
    {
        public HttpClient(HttpClientHandler h)
        {
            State.Step(1, "client");
        }
        public Task<Stream> GetStreamAsync(Uri uri)
        {
            State.Step(4, "get");
            return Task.FromResult<Stream>(new Body());
        }
        public void Dispose()
        {
            State.Step(7, "closeclient");
        }
    }
    class Body : MemoryStream
    {
        public override void CopyTo(Stream destination, int bufferSize)
        {
            State.Step(5, "copy");
            destination.Write(new byte[] { 0, 255, 13, 10, 65 });
        }
        protected override void Dispose(bool disposing)
        {
            State.Step(6, "closestream");
            base.Dispose(disposing);
        }
    }
}
class Credentials : ICredentials
{
    public NetworkCredential GetCredential(Uri u, string a) => null;
}
class Proxy : IWebProxy
{
    public ICredentials Credentials { get; set; }
    public Uri GetProxy(Uri u) => u;
    public bool IsBypassed(Uri u) => false;
}
class Program
{
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            State.Mask = int.Parse(a[0]);
            State.Trace = new();
            string output;
            try
            {
                var stream = System.Xml.XmlDownloadManager.GetStream(new Uri("http://example.invalid/x"),
                                                                     a[1] == "1" ? new Credentials() : null,
                                                                     a[2] == "1" ? new Proxy() : null);
                using var copy = new MemoryStream();
                stream.CopyTo(copy);
                output = Convert.ToHexString(copy.ToArray()).ToLowerInvariant();
            }
            catch (Exception e)
            {
                output = e.GetType().FullName + ":" + e.Message;
            }
            Console.WriteLine(output + "/" + string.Join(",", State.Trace));
        }
    }
}

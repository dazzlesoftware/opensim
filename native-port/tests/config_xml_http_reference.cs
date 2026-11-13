using System;
using System.IO;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Xml;
class Program
{
    sealed class TracedCredentials : System.Net.ICredentials
    {
        readonly System.Net.ICredentials inner;
        int basicLookups;
        public TracedCredentials(System.Net.ICredentials inner) => this.inner = inner;

        public System.Net.NetworkCredential GetCredential(Uri uri, string scheme)
        {
            Console.Error.WriteLine("CREDENTIAL " + uri.AbsolutePath + " " + scheme);
            if (uri.AbsolutePath == "/auth-scoped/stateful" && scheme == "Basic" && basicLookups++ == 0)
                return new System.Net.NetworkCredential("fixture", "unused");
            return inner.GetCredential(uri, scheme);
        }
    }

    static string H(string value)
    {
        var b = new System.Text.StringBuilder();
        foreach (char c in value)
            b.Append(((int)c).ToString("x4"));
        return b.Length == 0 ? "-" : b.ToString();
    }
    static string Error(Exception e)
    {
        var h = e as System.Net.Http.HttpRequestException;
        int code = h != null                                 ? (int)h.HttpRequestError
                   : e is System.Net.Http.HttpIOException io ? (int)io.HttpRequestError
                                                             : -1;
        var retry = e.GetType()
                        .GetProperty("AllowRetry", System.Reflection.BindingFlags.Instance |
                                                       System.Reflection.BindingFlags.NonPublic)
                        ?.GetValue(e)
                        ?.ToString() == "RetryOnConnectionFailure";
        return e.GetType().FullName + ":" + unchecked((uint)e.HResult).ToString("x8") + ":" + H(e.Message) +
               ":" + code + ":" +
               (h != null && h.StatusCode.HasValue ? ((int)h.StatusCode.Value).ToString() : "~") + ":" +
               (retry ? "1" : "0") + ":" + (e.InnerException == null ? "~" : Error(e.InnerException));
    }
    static void Meta(Exception e)
    {
        if (e is System.Net.Http.HttpRequestException)
            Console.Error.WriteLine(
                "RETRY:" + e.GetType()
                               .GetProperty("AllowRetry", System.Reflection.BindingFlags.Instance |
                                                              System.Reflection.BindingFlags.NonPublic)
                               ?.GetValue(e));
        if (e is System.Net.Sockets.SocketException s)
            Console.Error.WriteLine("SOCKET:" + s.NativeErrorCode + ":" + (int)s.SocketErrorCode + ":" +
                                    s.ErrorCode + ":" + s.HResult);
        if (e.InnerException != null)
            Meta(e.InnerException);
    }
    static Stream ActualStream(Uri uri, System.Net.ICredentials credentials, System.Net.IWebProxy proxy)
    {
        var method = typeof(XmlReader)
                         .Assembly.GetType("System.Xml.XmlDownloadManager", true)
                         .GetMethod("GetStream", System.Reflection.BindingFlags.NonPublic |
                                                     System.Reflection.BindingFlags.Static);
        try
        {
            return (Stream)method.Invoke(null, new object[] { uri, credentials, proxy });
        }
        catch (System.Reflection.TargetInvocationException e) when (e.InnerException != null)
        {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(e.InnerException).Throw();
            throw;
        }
    }
    static Stream AuthenticatedTlsFixture(Uri uri, System.Net.ICredentials credentials, System.Net.IWebProxy proxy)
    {
        using var handler = new System.Net.Http.HttpClientHandler
        {
            Credentials = credentials,
            UseProxy = proxy != null,
            Proxy = proxy,
            ServerCertificateCustomValidationCallback =
                System.Net.Http.HttpClientHandler.DangerousAcceptAnyServerCertificateValidator,
        };
        using var client = new System.Net.Http.HttpClient(handler);
        using var response = client.GetAsync(uri).GetAwaiter().GetResult();
        response.EnsureSuccessStatusCode();
        return new MemoryStream(response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult());
    }
    static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--credential-cache-host")
        {
            var cache = new System.Net.CredentialCache();
            cache.Add("Example.COM", 80, "bAsIc", new System.Net.NetworkCredential("fixture", "secret"));
            void Print(int port) => Console.WriteLine(cache.GetCredential("example.com", port, "BASIC")?.UserName ?? "-");
            Print(80);
            Print(81);
            cache.Remove("example.COM", 80, "Basic");
            Print(80);
            return;
        }
        if (args.Length == 2 && args[0] == "--create-proxy-certificate")
        {
            using var key = RSA.Create(2048);
            var request = new CertificateRequest("CN=fixture proxy", key, HashAlgorithmName.SHA256,
                                                 RSASignaturePadding.Pkcs1);
            var now = DateTimeOffset.UtcNow;
            using var certificate = request.CreateSelfSigned(now.AddDays(-1), now.AddDays(1));
            File.WriteAllText(Path.Combine(args[1], "certificate.pem"), certificate.ExportCertificatePem());
            File.WriteAllText(Path.Combine(args[1], "key.pem"), key.ExportPkcs8PrivateKeyPem());
            return;
        }
        System.Net.Http.HttpClient.DefaultProxy = new System.Net.WebProxy();
        bool tlsProxy = Array.IndexOf(args, "--tls-proxy") >= 0;
        string proxyScheme = tlsProxy ? "https" : "http";
        var proxy = args.Length >= 1 ? new System.Net.WebProxy(proxyScheme + "://127.0.0.1:" + args[0]) : null;
        if (Array.IndexOf(args, "--proxy-cache") >= 0)
        {
            var cache = new System.Net.CredentialCache();
            var credential = new System.Net.NetworkCredential("f\u00efxture", "p\u00e4ss");
            cache.Add(proxy.Address, "NTLM", credential);
            cache.Add(proxy.Address, "Negotiate", credential);
            cache.Add(proxy.Address, "Digest", credential);
            proxy.Credentials = cache;
        }
        else if (Array.IndexOf(args, "--proxy-auth") >= 0)
            proxy.Credentials = new System.Net.NetworkCredential("f\u00efxture", "p\u00e4ss");
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            try
            {
                var uri = new Uri(row);
                bool kerberos = uri.AbsolutePath == "/auth-kerberos";
                System.Net.ICredentials credentials = kerberos
                    ? new System.Net.NetworkCredential("fixture", "fixture-password", "OPENSIM.TEST")
                    : row.Contains("/auth-ntlm")
                        ? new System.Net.NetworkCredential("f\u00efxture", "p\u00e4ss")
                        : row.Contains("/auth-") ? new System.Net.NetworkCredential("fixture", "secret") : null;
                if (uri.AbsolutePath.StartsWith("/auth-scoped", StringComparison.Ordinal))
                {
                    var cache = new System.Net.CredentialCache();
                    cache.Add(new Uri(uri, "/auth-scoped/"), "Basic",
                              new System.Net.NetworkCredential("fixture", "secret"));
                    if (uri.AbsolutePath.EndsWith("/cache-redirect", StringComparison.Ordinal))
                    {
                        cache = new System.Net.CredentialCache();
                        cache.Add(new Uri(uri, "/"), "bAsIc", new System.Net.NetworkCredential("fixture", "wrong"));
                        cache.Add(new Uri(uri, "/AUTH-SCOPED/file"), "bAsIc",
                                  new System.Net.NetworkCredential("fixture", "secret"));
                        cache.Add(new Uri(uri, "/AUTH-SCOPED/a-longer-file"), "bAsIc",
                                  new System.Net.NetworkCredential("fixture", "removed"));
                        cache.Remove(new Uri(uri, "/AUTH-SCOPED/a-longer-file#fragment"), "BASIC");
                        credentials = cache;
                    }
                    else
                        credentials = new TracedCredentials(cache);
                }
                else if (uri.AbsolutePath == "/auth-default")
                    credentials = System.Net.CredentialCache.DefaultCredentials;
                using var stream = (kerberos || uri.AbsolutePath == "/auth-ntlm") &&
                                   (uri.Scheme == "https" || tlsProxy)
                    ? AuthenticatedTlsFixture(uri, credentials, proxy) : ActualStream(uri, credentials, proxy);
                using var memory = new MemoryStream();
                stream.CopyTo(memory);
                Console.WriteLine("ok:" + Convert.ToHexString(memory.ToArray()).ToLowerInvariant());
            }
            catch (Exception e)
            {
                Meta(e);
                Console.Error.WriteLine(
                    row + " " +
                    e.GetType()
                        .GetProperty("AllowRetry", System.Reflection.BindingFlags.NonPublic |
                                                       System.Reflection.BindingFlags.Instance)
                        ?.GetValue(e));
                Console.WriteLine(Error(e));
            }
        }
    }
}

using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
static partial class WebUtil
{
    public static SocketsHttpHandler SharedSocketsHttpHandlerNoRedir, SharedSocketsHttpHandler;
}
class NeverProxy : IWebProxy
{
    public ICredentials Credentials { get; set; }
    public Uri GetProxy(Uri uri) => throw new Exception("Unexpected default proxy query");
    public bool IsBypassed(Uri uri) => throw new Exception("Unexpected default proxy bypass query");
}
class FixedProxy : IWebProxy
{
    public Uri Address;
    public ICredentials Credentials { get; set; }
    public Uri GetProxy(Uri uri) => Address;
    public bool IsBypassed(Uri uri) => false;
}
sealed class CaptureTlsStream : Stream
{
    public MemoryStream Bytes = new();
    public override bool CanRead => true;
    public override bool CanWrite => true;
    public override bool CanSeek => false;
    public override long Length => Bytes.Length;
    public override long Position
    {
        get => 0;
        set => throw new NotSupportedException();
    }
    public override void Flush()
    {
    }
    public override int Read(byte[] b, int o, int n) => 0;
    public override int Read(Span<byte> b) => 0;
    public override ValueTask<int> ReadAsync(Memory<byte> b, CancellationToken c = default) => new(0);
    public override void Write(byte[] b, int o, int n) => Bytes.Write(b, o, n);
    public override void Write(ReadOnlySpan<byte> b) => Bytes.Write(b);
    public override ValueTask WriteAsync(ReadOnlyMemory<byte> b, CancellationToken c = default)
    {
        Bytes.Write(b.Span);
        return ValueTask.CompletedTask;
    }
    public override long Seek(long n, SeekOrigin o) => throw new NotSupportedException();
    public override void SetLength(long n) => throw new NotSupportedException();
}
class Program
{
    static void HelloProbe()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            WebUtil.SetupHTTPClients(true, true, null, 32);
            var stream = new CaptureTlsStream();
            WebUtil.SharedSocketsHttpHandler.ConnectCallback = (_, _) => new ValueTask<Stream>(stream);
            using var client = WebUtil.GetNewGlobalHttpClient(2000);
            using var request = new HttpRequestMessage(HttpMethod.Get, "https://127.0.0.1:1/");
            if (line != "-")
                request.Headers.TryAddWithoutValidation("Host", HostDecode(line));
            try
            {
                using var response = client.Send(request, HttpCompletionOption.ResponseHeadersRead);
            }
            catch
            {
            }
            Console.WriteLine("Y " + Hex(stream.Bytes.ToArray()));
            WebUtil.SharedSocketsHttpHandler.Dispose();
            WebUtil.SharedSocketsHttpHandlerNoRedir.Dispose();
        }
    }
    static void CertificateProbe()
    {
        var normalize = typeof(SslStream)
                            .Assembly.GetType("System.Net.Security.TargetHostNameHelper")
                            .GetMethod("NormalizeHostName", System.Reflection.BindingFlags.Static |
                                                                System.Reflection.BindingFlags.NonPublic);
        var module = typeof(SslStream).Assembly;
        var validate =
            (module.GetType("System.Net.CertificateValidation") ??
             module.GetType("System.Net.Security.CertificateValidation"))
                .GetMethod("BuildChainAndVerifyProperties",
                           System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                var fields = line.Split(' ');
                using var cert = new X509Certificate2(Convert.FromHexString(fields[0]));
                using var chain = new X509Chain();
                chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
                chain.ChainPolicy.CustomTrustStore.Add(cert);
                chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
                chain.ChainPolicy.DisableCertificateDownloads = true;
                var name = (string)normalize.Invoke(null, new object[] { HostDecode(fields[1]) });
                var errors =
                    (SslPolicyErrors)validate.Invoke(null, new object[] { chain, cert, true, false, name });
                Console.WriteLine("C " +
                                  (((errors & SslPolicyErrors.RemoteCertificateNameMismatch) == 0) ? 1 : 0));
            }
            catch
            {
                Console.WriteLine("C E");
            }
        }
    }
    static void NameCertificates(string path)
    {
        Directory.CreateDirectory(path);
        using var key = RSA.Create(2048);
        foreach (var identity in new[] { "ip", "dns", "cn", "cn-noncanonical", "cn-ip-dns", "cn-ip-other-ip",
                                         "cn-ip-multiple", "partial-wild", "cn-multi-dns" })
        {
            string subject =
                identity switch { "cn" or "cn-ip-dns" or "cn-ip-other-ip" => "CN=127.0.0.1",
                                  "cn-noncanonical" => "CN=127.1",
                                  "cn-ip-multiple" => "CN=127.0.0.1,CN=127.1",
                                  "cn-multi-dns" => "CN=first.fixture.test,CN=second.fixture.test",
                                  _ => "CN=unrelated" };
            var request =
                new CertificateRequest(subject, key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
            request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
            if (identity is "ip" or "dns" or "cn-ip-dns" or "cn-ip-other-ip" or "partial-wild")
            {
                var san = new SubjectAlternativeNameBuilder();
                if (identity == "ip")
                {
                    san.AddIpAddress(IPAddress.Parse("127.0.0.1"));
                    san.AddIpAddress(IPAddress.Parse("::1"));
                    san.AddIpAddress(IPAddress.Parse("fe80::1"));
                }
                else if (identity == "cn-ip-other-ip")
                    san.AddIpAddress(IPAddress.Parse("127.0.0.2"));
                else if (identity == "cn-ip-dns")
                    san.AddDnsName("unrelated.fixture.test");
                else if (identity == "partial-wild")
                    san.AddDnsName("a*.fixture.test");
                else
                {
                    san.AddDnsName("127.1");
                    san.AddDnsName("127.0.0.1");
                    san.AddDnsName("alt.fixture.test");
                    san.AddDnsName("*.wild.fixture.test");
                }
                request.CertificateExtensions.Add(san.Build());
            }
            using var cert =
                request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
            File.WriteAllBytes(Path.Combine(path, identity + ".der"), cert.Export(X509ContentType.Cert));
        }
    }
    static string HostEncode(string value) =>
        value == null ? "-"
        : value.Length == 0
            ? "_"
            : string.Concat(Array.ConvertAll(value.ToCharArray(), c => ((int)c).ToString("x4")));
    static string HostDecode(string value)
    {
        if (value == "-")
            return null;
        if (value == "_")
            return "";
        var chars = new char[value.Length / 4];
        for (int i = 0; i < chars.Length; i++)
            chars[i] = (char)Convert.ToInt32(value.Substring(i * 4, 4), 16);
        return new string(chars);
    }
    static void HostProbe()
    {
        var normalize = typeof(SslStream)
                            .Assembly.GetType("System.Net.Security.TargetHostNameHelper")
                            .GetMethod("NormalizeHostName", System.Reflection.BindingFlags.Static |
                                                                System.Reflection.BindingFlags.NonPublic);
        var key = typeof(HttpClient)
                      .Assembly.GetType("System.Net.Http.HttpConnectionPoolManager")
                      .GetMethod("ParseHostNameFromHeader", System.Reflection.BindingFlags.Static |
                                                                System.Reflection.BindingFlags.NonPublic);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                var fields = line.Split(' ');
                if (fields[0] == "I")
                    Console.WriteLine("I " + HostEncode(new System.Globalization.IdnMapping().GetAscii(
                                                 HostDecode(fields[1]))));
                else if (fields[0] == "N")
                    Console.WriteLine("N " + HostEncode((string)normalize.Invoke(
                                                 null, new object[] { HostDecode(fields[1]) })));
                else
                {
                    using var request = new HttpRequestMessage();
                    request.Headers.TryAddWithoutValidation("Host",
                                                            Array.ConvertAll(fields[1..], HostDecode));
                    var host = request.Headers.Host;
                    Console.WriteLine(
                        "H " + HostEncode(host) + " " +
                        HostEncode(host == null ? null : (string)key.Invoke(null, new object[] { host })));
                }
            }
            catch (Exception e)
            {
                if (e is System.Reflection.TargetInvocationException)
                    e = e.InnerException;
                Console.WriteLine("E " + e.GetType().FullName + " " + e.HResult.ToString("x8") + " " +
                                  HostEncode(e.Message));
            }
        }
    }
    static string Hex(byte[] bytes) => bytes.Length == 0 ? "-"
                                                         : Convert.ToHexString(bytes).ToLowerInvariant();
    static string Message(Exception e) => Hex(System.Text.Encoding.UTF8.GetBytes(e.Message));
    static void Certificates(string path, string aiaUrl = null)
    {
        Directory.CreateDirectory(path);
        var now = DateTimeOffset.UtcNow;
        using var ka = RSA.Create(2048);
        using var kb = RSA.Create(2048);
        X509Certificate2 Root(string name, RSA key)
        {
            var r = new CertificateRequest("CN=" + name, key, HashAlgorithmName.SHA256,
                                           RSASignaturePadding.Pkcs1);
            r.CertificateExtensions.Add(new X509BasicConstraintsExtension(true, false, 0, true));
            r.CertificateExtensions.Add(
                new X509KeyUsageExtension(X509KeyUsageFlags.KeyCertSign | X509KeyUsageFlags.CrlSign, true));
            r.CertificateExtensions.Add(new X509SubjectKeyIdentifierExtension(r.PublicKey, false));
            return r.CreateSelfSigned(now.AddYears(-1), now.AddYears(3));
        }
        using var ra = Root("Native fixture root A", ka);
        using var rb = Root("Native fixture root B", kb);
        File.WriteAllText(Path.Combine(path, "root.pem"), ra.ExportCertificatePem());
        if (aiaUrl != null)
        {
            using var issuerKey = RSA.Create(2048);
            var issuerRequest = new CertificateRequest("CN=AIA intermediate", issuerKey,
                                                       HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
            issuerRequest.CertificateExtensions.Add(new X509BasicConstraintsExtension(true, false, 0, true));
            issuerRequest.CertificateExtensions.Add(
                new X509KeyUsageExtension(X509KeyUsageFlags.KeyCertSign, true));
            using var issued = issuerRequest.Create(ra, now.AddDays(-10), now.AddDays(10),
                                                    RandomNumberGenerator.GetBytes(16));
            using var issuer = issued.CopyWithPrivateKey(issuerKey);
            using var leafKey = RSA.Create(2048);
            var request = new CertificateRequest("CN=AIA fixture", leafKey, HashAlgorithmName.SHA256,
                                                 RSASignaturePadding.Pkcs1);
            var san = new SubjectAlternativeNameBuilder();
            san.AddIpAddress(IPAddress.Loopback);
            request.CertificateExtensions.Add(san.Build());
            var writer = new System.Formats.Asn1.AsnWriter(System.Formats.Asn1.AsnEncodingRules.DER);
            using (writer.PushSequence()) using (writer.PushSequence())
            {
                writer.WriteObjectIdentifier("1.3.6.1.5.5.7.48.2");
                writer.WriteCharacterString(
                    System.Formats.Asn1.UniversalTagNumber.IA5String, aiaUrl,
                    new System.Formats.Asn1.Asn1Tag(System.Formats.Asn1.TagClass.ContextSpecific, 6));
            }
            request.CertificateExtensions.Add(new X509Extension("1.3.6.1.5.5.7.1.1", writer.Encode(), false));
            using var leaf =
                request.Create(issuer, now.AddDays(-1), now.AddDays(1), RandomNumberGenerator.GetBytes(16));
            File.WriteAllText(Path.Combine(path, "aia.pem"), leaf.ExportCertificatePem());
            File.WriteAllText(Path.Combine(path, "aia.key.pem"), leafKey.ExportPkcs8PrivateKeyPem());
            File.WriteAllBytes(Path.Combine(path, "issuer.der"), issuer.RawData);
            return;
        }
        foreach (var name in new[] { "valid", "wrong-name", "untrusted", "untrusted-wrong-name", "expired",
                                     "dns", "cn-ip-dns", "cn-noncanonical", "partial-wild" })
        {
            using var key = RSA.Create(2048);
            var subject = name == "cn-ip-dns"         ? "CN=127.0.0.1"
                          : name == "cn-noncanonical" ? "CN=127.1"
                                                      : "CN=Native fixture leaf";
            var request =
                new CertificateRequest(subject, key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
            request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
            request.CertificateExtensions.Add(new X509KeyUsageExtension(
                X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, true));
            var eku = new OidCollection();
            eku.Add(new Oid("1.3.6.1.5.5.7.3.1"));
            request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(eku, false));
            request.CertificateExtensions.Add(
                new X509SubjectKeyIdentifierExtension(request.PublicKey, false));
            var san = new SubjectAlternativeNameBuilder();
            if (name == "dns")
            {
                san.AddDnsName("alt.fixture.test");
                san.AddDnsName("mixed.fixture.test");
                san.AddDnsName("xn--bcher-kva.fixture.test");
            }
            else if (name == "cn-ip-dns")
                san.AddDnsName("unrelated.fixture.test");
            else if (name == "partial-wild")
                san.AddDnsName("a*.fixture.test");
            else
                san.AddIpAddress(IPAddress.Parse(
                    name.Contains("wrong-name") || name == "cn-noncanonical" ? "127.0.0.2" : "127.0.0.1"));
            request.CertificateExtensions.Add(san.Build());
            var issuer = name.StartsWith("untrusted") ? rb : ra;
            using var publicCert = request.Create(issuer, now.AddDays(-10),
                                                  name == "expired" ? now.AddDays(-5) : now.AddDays(10),
                                                  RandomNumberGenerator.GetBytes(16));
            File.WriteAllText(Path.Combine(path, name + ".pem"),
                              publicCert.ExportCertificatePem() + "\n" + issuer.ExportCertificatePem());
            File.WriteAllText(Path.Combine(path, name + ".key.pem"), key.ExportPkcs8PrivateKeyPem());
        }
    }
    static void Main(string[] args)
    {
        if (args[0] == "--host-probe")
        {
            HostProbe();
            return;
        }
        if (args[0] == "--hello-probe")
        {
            HelloProbe();
            return;
        }
        if (args[0] == "--certificate-probe")
        {
            CertificateProbe();
            return;
        }
        if (args[0] == "--make-name-certs")
        {
            NameCertificates(args[1]);
            return;
        }
        if (args[0] == "--make-certs")
        {
            Certificates(args[1], args.Length > 2 ? args[2] : null);
            return;
        }
        HttpClient.DefaultProxy = new NeverProxy();
        string root = args[1];
        FixedProxy proxy = null;
        var clients = new Dictionary<string, HttpClient>();
        var streams = new Dictionary<string, (HttpResponseMessage, Stream)>();
        var jobs = new Dictionary<string, Task<string>>();
        var mutex = new object();
        string Open(string id, HttpClient client, string path, string method, string agent, string host)
        {
            try
            {
                var request = new HttpRequestMessage(new HttpMethod(method),
                                                     path.StartsWith("http") ? path : args[0] + path);
                if (!string.IsNullOrEmpty(agent) && agent != "-")
                    request.Headers.TryAddWithoutValidation("User-Agent", agent);
                if (host != null)
                    request.Headers.TryAddWithoutValidation("Host", host);
                var response = client.Send(request, HttpCompletionOption.ResponseHeadersRead);
                var stream = response.Content.ReadAsStream();
                lock (mutex) streams[id] = (response, stream);
                return $"O {id} {(int)response.StatusCode} {string.Join("",response.Headers.GetValues("X-Connection-Id"))}";
            }
            catch (Exception e)
            {
                return "E " + id + " " + e.GetType().FullName + " " + Message(e);
            }
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            var id = a[1];
            switch (a[0])
            {
            case "P":
                proxy ??= new FixedProxy();
                proxy.Address = new Uri(id);
                Console.WriteLine("P");
                break;
            case "S": {
                int flags = int.Parse(id);
                WebUtil.SetupHTTPClients((flags & 1) != 0, (flags & 2) != 0, proxy, int.Parse(a[2]));
                foreach (var h in new[] { WebUtil.SharedSocketsHttpHandler,
                                          WebUtil.SharedSocketsHttpHandlerNoRedir })
                {
                    if (root != "-")
                    {
                        var policy = new X509ChainPolicy { TrustMode = X509ChainTrustMode.CustomRootTrust,
                                                           RevocationMode = X509RevocationMode.NoCheck,
                                                           DisableCertificateDownloads = true };
                        var trusted = X509Certificate2.CreateFromPem(File.ReadAllText(root));
                        policy.CustomTrustStore.Add(trusted);
                        policy.ExtraStore.Add(trusted);
                        h.SslOptions.CertificateChainPolicy = policy;
                    }
                    var original = h.SslOptions.RemoteCertificateValidationCallback;
                    h.SslOptions.RemoteCertificateValidationCallback = (sender, certificate, chain, errors) =>
                    {
                        if (certificate != null)
                            Console.Error.WriteLine(
                                "PEER_CERT:" +
                                Convert.ToHexString(SHA256.HashData(certificate.GetRawCertData())) + ":" +
                                certificate.Issuer);
                        return original(sender, certificate, chain, errors);
                    };
                }
                Console.WriteLine("S");
                break;
            }
            case "G": {
                clients[id] = a[2] == "1"
                                  ? WebUtil.GetGlobalNoRedirHttpClient(int.Parse(a[3]), int.Parse(a[4]))
                                  : WebUtil.GetNewGlobalHttpClient(int.Parse(a[3]), int.Parse(a[4]));
                Console.WriteLine("G " + id);
                break;
            }
            case "D":
                clients[id].Dispose();
                Console.WriteLine("D " + id);
                break;
            case "W":
                Thread.Sleep(int.Parse(id));
                break;
            case "O":
                Console.WriteLine(Open(id, clients[a[2]], a[3], a[4], a.Length > 5 ? a[5] : null,
                                       a.Length > 6 ? HostDecode(a[6]) : null));
                break;
            case "A":
                jobs[id] = Task.Run(() => Open(id, clients[a[2]], a[3], a[4], a.Length > 5 ? a[5] : null,
                                               a.Length > 6 ? HostDecode(a[6]) : null));
                break;
            case "J":
                Console.WriteLine(jobs[id].GetAwaiter().GetResult());
                jobs.Remove(id);
                break;
            case "R": {
                Stream stream;
                lock (mutex) stream = streams[id].Item2;
                int count = int.Parse(a[2]);
                using var output = new MemoryStream();
                var buffer = new byte[4096];
                while (count < 0 || output.Length < count)
                {
                    int n = stream.Read(buffer, 0,
                                        count < 0 ? buffer.Length
                                                  : Math.Min(buffer.Length, count - (int)output.Length));
                    if (n == 0)
                        break;
                    output.Write(buffer, 0, n);
                }
                Console.WriteLine("R " + id + " " + Hex(output.ToArray()));
                break;
            }
            case "C": {
                (HttpResponseMessage, Stream) item;
                lock (mutex)
                {
                    item = streams[id];
                    streams.Remove(id);
                }
                item.Item2.Dispose();
                item.Item1.Dispose();
                Console.WriteLine("C " + id);
                break;
            }
            }
        }
        foreach (var s in streams.Values)
        {
            s.Item2.Dispose();
            s.Item1.Dispose();
        }
        foreach (var c in clients.Values)
            c.Dispose();
    }
}

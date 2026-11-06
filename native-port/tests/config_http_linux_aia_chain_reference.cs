using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Formats.Asn1;
using System.Collections.Generic;
using System.Text.Json;
class Program
{
    static readonly BindingFlags Instance =
                                     BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                                 Static = BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    [DllImport("libcrypto.so.3")]
    static extern IntPtr X509_STORE_CTX_get0_chain(IntPtr context);
    [DllImport("libcrypto.so.3")]
    static extern int OPENSSL_sk_num(IntPtr stack);
    [DllImport("libcrypto.so.3")]
    static extern IntPtr OPENSSL_sk_value(IntPtr stack, int index);
    sealed record Reply(string uri, string der, int delay = 0);
    sealed record Row(string label, string leaf, string[] roots, string[] extra, string[] manual, int mode,
                      int timeout, Reply[] replies);
    static string Hex(byte[] bytes) => Convert.ToHexString(bytes).ToLowerInvariant();
    static string Flags(IEnumerable<X509ChainStatus> values)
    {
        var result = string.Join(",", values.Select(s => ((uint)s.Status).ToString()));
        return result.Length == 0 ? "-" : result;
    }
    static void Main(string[] args)
    {
        var file = args[1];
        if (args[0] == "--prepare")
        {
            int port = int.Parse(args[2]), serial = 1;
            using var rootKey = RSA.Create(2048);
            using var upperKey = RSA.Create(2048);
            using var issuerKey = RSA.Create(2048);
            using var leafKey = RSA.Create(2048);
            using var wrongKey = RSA.Create(2048);
            byte[] Aia(params string[] uris)
            {
                var w = new AsnWriter(AsnEncodingRules.DER);
                w.PushSequence();
                foreach (var uri in uris)
                {
                    w.PushSequence();
                    w.WriteObjectIdentifier("1.3.6.1.5.5.7.48.2");
                    w.WriteCharacterString(UniversalTagNumber.IA5String, uri,
                                           new Asn1Tag(TagClass.ContextSpecific, 6));
                    w.PopSequence();
                }
                w.PopSequence();
                return w.Encode();
            }
            X509Certificate2 Cert(string name, RSA key, X509Certificate2 issuer, RSA signing, bool ca = false,
                                  byte[][] aia = null)
            {
                var r = new CertificateRequest("CN=" + name, key, HashAlgorithmName.SHA256,
                                               RSASignaturePadding.Pkcs1);
                r.CertificateExtensions.Add(new X509BasicConstraintsExtension(ca, false, 0, true));
                r.CertificateExtensions.Add(
                    new X509KeyUsageExtension(ca ? X509KeyUsageFlags.KeyCertSign | X509KeyUsageFlags.CrlSign
                                                 : X509KeyUsageFlags.DigitalSignature,
                                              true));
                if (aia != null)
                    foreach (var data in aia)
                        r.CertificateExtensions.Add(new X509Extension("1.3.6.1.5.5.7.1.1", data, false));
                return r.Create(
                    issuer?.SubjectName ?? r.SubjectName,
                    X509SignatureGenerator.CreateForRSA(signing ?? key, RSASignaturePadding.Pkcs1),
                    new DateTimeOffset(2020, 1, 1, 0, 0, 0, TimeSpan.Zero),
                    new DateTimeOffset(2040, 1, 1, 0, 0, 0, TimeSpan.Zero), BitConverter.GetBytes(serial++));
            }
            using var root = Cert("Root", rootKey, null, null, true);
            using var issuer = Cert("Issuer", issuerKey, root, rootKey, true);
            using var upper = Cert("Upper", upperKey, root, rootKey, true);
            using var wrong = Cert("Unrelated", wrongKey, root, rootKey, true);
            using var badIssuer = Cert("Issuer", wrongKey, root, rootKey, true);
            var rows = new List<Row>();
            string Url(string label, string part) => "http://127.0.0.1:" + port + "/" + label + "/" + part;
            void Add(string label, byte[][] ext, Reply[] replies = null, string[] extra = null)
            {
                using var leaf = Cert("fixture.test", leafKey, issuer, issuerKey, aia: ext);
                rows.Add(new(label, Hex(leaf.RawData), new[] { Hex(root.RawData) },
                             extra ?? Array.Empty<string>(), Array.Empty<string>(), 0, 1000,
                             replies ?? Array.Empty<Reply>()));
            }
            Add("no-aia", null);
            Add("empty-aia", new[] { Aia() });
            Add("malformed-aia", new[] { new byte[] { 255 } });
            Add("https-only", new[] { Aia("https://127.0.0.1:" + port + "/ignored") });
            Add("https-then-http",
                new[] { Aia("https://127.0.0.1:" + port + "/ignored", Url("https-then-http", "issuer")) },
                new[] { new Reply(Url("https-then-http", "issuer"), Hex(issuer.RawData)) });
            foreach (var label in new[] { "single", "wrong-first", "empty-first", "wrong-key",
                                          "already-supplied", "bad-supplied", "seed-unrelated" })
            {
                var first = Url(label, "first");
                var second = Url(label, "second");
                var replies = new[] { new Reply(first, label == "wrong-first"   ? Hex(wrong.RawData)
                                                       : label == "empty-first" ? null
                                                       : label == "wrong-key"   ? Hex(badIssuer.RawData)
                                                                                : Hex(issuer.RawData)),
                                      new Reply(second, Hex(issuer.RawData)) };
                Add(label, new[] { Aia(first, second) }, replies,
                    label == "already-supplied" ? new[] { Hex(issuer.RawData) }
                    : label == "bad-supplied"   ? new[] { Hex(badIssuer.RawData) }
                                                : null);
                if (label == "seed-unrelated")
                    rows[^1] = rows[^1] with { manual = new[] { Hex(wrong.RawData) }, mode = 2 };
            }
            foreach (var label in new[] { "two-level", "per-download-timeout" })
            {
                var upperUrl = Url(label, "upper");
                var issuerUrl = Url(label, "issuer");
                using var lower = Cert("Issuer", issuerKey, upper, upperKey, true, new[] { Aia(upperUrl) });
                using var leaf =
                    Cert("fixture.test", leafKey, lower, issuerKey, aia: new[] { Aia(issuerUrl) });
                var delay = label == "per-download-timeout" ? 650 : 0;
                rows.Add(new(label, Hex(leaf.RawData), new[] { Hex(root.RawData) }, Array.Empty<string>(),
                             Array.Empty<string>(), 0, 1000,
                             new[] { new Reply(issuerUrl, Hex(lower.RawData), delay),
                                     new Reply(upperUrl, Hex(upper.RawData), delay) }));
            }
            using (var lower = Cert("Issuer", issuerKey, upper, upperKey, true)) using (
                var leaf = Cert("fixture.test", leafKey, lower, issuerKey))
                rows.Add(new("manual-duplicate-instances", Hex(leaf.RawData), new[] { Hex(root.RawData) },
                             Array.Empty<string>(),
                             new[] { Hex(upper.RawData), Hex(upper.RawData), Hex(lower.RawData) }, 1, 1000,
                             Array.Empty<Reply>()));
            File.WriteAllText(file, JsonSerializer.Serialize(rows));
            return;
        }
        var cases = JsonSerializer.Deserialize<Row[]>(File.ReadAllText(file));
        var type = typeof(X509Certificate2)
                       .Assembly.GetType(
                           "System.Security.Cryptography.X509Certificates.OpenSslX509ChainProcessor", true);
        var output = new List<object>();
        int Code(object value) =>
            Convert.ToInt32(value.GetType().GetProperty("UniversalCode", Instance).GetValue(value));
        foreach (var row in cases)
        {
            using var leaf = new X509Certificate2(Convert.FromHexString(row.leaf));
            var roots = new X509Certificate2Collection();
            foreach (var der in row.roots)
                roots.Add(new X509Certificate2(Convert.FromHexString(der)));
            var extra = new X509Certificate2Collection();
            foreach (var der in row.extra)
                extra.Add(new X509Certificate2(Convert.FromHexString(der)));
            var manual = new List<X509Certificate2>();
            object processor = null;
            object Initiate()
            {
                var pal = typeof(X509Certificate).GetProperty("Pal", Instance).GetValue(leaf);
                var handle = pal.GetType().GetProperty("SafeHandle", Instance).GetValue(pal);
                return type.GetMethod("InitiateChain", Static)
                    .Invoke(null, new object[] { handle, roots, X509ChainTrustMode.CustomRootTrust,
                                                 new DateTime(2030, 1, 1, 0, 0, 0, DateTimeKind.Utc),
                                                 TimeSpan.FromMilliseconds(row.timeout) });
            }
            processor = Initiate();
            int first = Code(
                    type.GetMethod("FindFirstChain", Instance).Invoke(processor, new object[] { extra })),
                last = first;
            var downloaded = new List<X509Certificate2>();
            var selected = new List<int>();
            if (row.mode != 0)
                foreach (var der in row.manual)
                {
                    var cert = new X509Certificate2(Convert.FromHexString(der));
                    manual.Add(cert);
                    extra.Add(cert);
                    ((IDisposable)processor).Dispose();
                    processor = Initiate();
                    last = Code(
                        type.GetMethod("FindFirstChain", Instance).Invoke(processor, new object[] { extra }));
                }
            if (row.mode == 1)
            {
                var context = (SafeHandle)type.GetField("_storeCtx", Instance).GetValue(processor);
                var chain = X509_STORE_CTX_get0_chain(context.DangerousGetHandle());
                for (int i = 0; i < manual.Count; i++)
                    for (int j = 0; j < OPENSSL_sk_num(chain); j++)
                        if (manual[i].Handle == OPENSSL_sk_value(chain, j))
                        {
                            selected.Add(i);
                            break;
                        }
            }
            var watch = System.Diagnostics.Stopwatch.StartNew();
            if (row.mode != 1 && (last == 2 || last == 20))
            {
                var parameters = new object[] { null };
                last = Code(type.GetMethod("FindChainViaAia", Instance).Invoke(processor, parameters));
                downloaded = (List<X509Certificate2>)parameters[0] ?? new();
            }
            watch.Stop();
            type.GetMethod("CommitToChain", Instance).Invoke(processor, null);
            type.GetMethod("Finish", Instance)
                .Invoke(processor, new object[] { new OidCollection(), new OidCollection() });
            var status = (X509ChainStatus[])type.GetProperty("ChainStatus", Instance).GetValue(processor);
            var elements =
                (X509ChainElement[])type.GetProperty("ChainElements", Instance).GetValue(processor);
            var expected =
                first + " " + last + " D:" +
                (downloaded.Count == 0 ? "-" : string.Join(",", downloaded.Select(c => Hex(c.RawData)))) +
                " I:" + (selected.Count == 0 ? "-" : string.Join(",", selected)) + " F:" + Flags(status);
            foreach (var element in elements)
                expected += " " + Hex(element.Certificate.RawData) + ":" + Flags(element.ChainElementStatus);
            output.Add(new { row.label, expected, downloadElapsedMilliseconds = watch.ElapsedMilliseconds });
            ((IDisposable)processor).Dispose();
            foreach (var cert in downloaded)
                cert.Dispose();
            foreach (var cert in roots)
                cert.Dispose();
            foreach (var cert in extra)
                cert.Dispose();
        }
        Console.WriteLine(JsonSerializer.Serialize(output));
    }
}

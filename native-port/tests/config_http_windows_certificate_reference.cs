using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Security;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
class Program
{
    static readonly DateTimeOffset Start = new(2020, 1, 1, 0, 0, 0, TimeSpan.Zero),
                                   End = new(2040, 1, 1, 0, 0, 0, TimeSpan.Zero),
                                   At = new(2030, 1, 1, 0, 0, 0, TimeSpan.Zero);
    static readonly RSA RootKey = RSA.Create(2048), IntermediateKey = RSA.Create(2048),
                        LeafKey = RSA.Create(2048);
    static int serial = 1;
    static X509Certificate2 Cert(string subject, RSA key, X509Certificate2 issuer, RSA issuerKey,
                                 bool ca = false, int? path = null, string eku = "server", string san = "dns",
                                 DateTimeOffset? start = null, DateTimeOffset? end = null,
                                 bool critical = false)
    {
        var r =
            new CertificateRequest("CN=" + subject, key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        r.CertificateExtensions.Add(new X509BasicConstraintsExtension(ca, path.HasValue, path ?? 0, true));
        r.CertificateExtensions.Add(new X509KeyUsageExtension(
            ca ? X509KeyUsageFlags.KeyCertSign | X509KeyUsageFlags.CrlSign
               : X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment,
            true));
        if (eku != "none")
            r.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(
                new OidCollection { new Oid(eku == "client" ? "1.3.6.1.5.5.7.3.2"
                                            : eku == "any"  ? "2.5.29.37.0"
                                                            : "1.3.6.1.5.5.7.3.1") },
                false));
        if (san != "none")
        {
            var b = new SubjectAlternativeNameBuilder();
            if (san == "ip")
            {
                b.AddIpAddress(System.Net.IPAddress.Loopback);
                b.AddIpAddress(System.Net.IPAddress.IPv6Loopback);
            }
            else
                b.AddDnsName(san == "wild" ? "*.fixture.test" : "fixture.test");
            r.CertificateExtensions.Add(b.Build());
        }
        if (critical)
            r.CertificateExtensions.Add(
                new X509Extension("1.3.6.1.4.1.55555.9.8", new byte[] { 5, 0 }, true));
        if (issuer == null)
            return r.CreateSelfSigned(start ?? Start, end ?? End);
        return r.Create(issuer.SubjectName,
                        X509SignatureGenerator.CreateForRSA(issuerKey, RSASignaturePadding.Pkcs1),
                        start ?? Start, end ?? End, BitConverter.GetBytes(serial++));
    }
    static string Hex(byte[] x) => Convert.ToHexString(x).ToLowerInvariant();
    static void Main()
    {
        using var root = Cert("Root", RootKey, null, null, true, null, "none", "none");
        using var wrongRoot = Cert("Wrong Root", RootKey, null, null, true, null, "none", "none");
        using var intermediate =
            Cert("Intermediate", IntermediateKey, root, RootKey, true, 0, "none", "none");
        using var noCa = Cert("Not CA", IntermediateKey, root, RootKey, false, null, "none", "none");
        using var root0 = Cert("Root Zero", RootKey, null, null, true, 0, "none", "none");
        using var intermediate0 =
            Cert("Intermediate Zero", IntermediateKey, root0, RootKey, true, 0, "none", "none");
        var cases = new List<(string label, byte[] leaf, byte[][] extra, byte[][] roots)>();
        void Add(string label, X509Certificate2 leaf, byte[][] extra = null, byte[][] roots = null) =>
            cases.Add((label, leaf.RawData, extra ?? Array.Empty<byte[]>(), roots ?? new[] { root.RawData }));
        foreach (var eku in new[] { "server", "client", "any", "none" })
            Add("eku-" + eku, Cert("fixture.test", LeafKey, root, RootKey, eku: eku));
        foreach (var san in new[] { "dns", "wild", "ip", "none" })
            Add("san-" + san, Cert("fixture.test", LeafKey, root, RootKey, san: san));
        Add("expired", Cert("fixture.test", LeafKey, root, RootKey,
                            end: new DateTimeOffset(2021, 1, 1, 0, 0, 0, TimeSpan.Zero)));
        Add("future", Cert("fixture.test", LeafKey, root, RootKey,
                           start: new DateTimeOffset(2031, 1, 1, 0, 0, 0, TimeSpan.Zero)));
        Add("unknown-critical", Cert("fixture.test", LeafKey, root, RootKey, critical: true));
        Add("ca-leaf", Cert("fixture.test", LeafKey, root, RootKey, true, null, "none"));
        Add("intermediate", Cert("fixture.test", LeafKey, intermediate, IntermediateKey),
            new[] { intermediate.RawData });
        Add("intermediate-missing", Cert("fixture.test", LeafKey, intermediate, IntermediateKey));
        Add("intermediate-duplicate", Cert("fixture.test", LeafKey, intermediate, IntermediateKey),
            new[] { root.RawData, intermediate.RawData, intermediate.RawData });
        Add("issuer-not-ca", Cert("fixture.test", LeafKey, noCa, IntermediateKey), new[] { noCa.RawData });
        Add("path-length", Cert("fixture.test", LeafKey, intermediate0, IntermediateKey),
            new[] { intermediate0.RawData }, new[] { root0.RawData });
        Add("wrong-root", Cert("fixture.test", LeafKey, root, RootKey), roots: new[] { wrongRoot.RawData });
        Add("empty-roots", Cert("fixture.test", LeafKey, root, RootKey), roots: Array.Empty<byte[]>());
        using var self = Cert("fixture.test", LeafKey, null, null);
        Add("self-leaf-trusted", self, roots: new[] { self.RawData });
        Add("self-leaf-untrusted", self);
        var damaged = cases[0].leaf.ToArray();
        damaged[^1] ^= 1;
        cases.Add(("bad-signature", damaged, Array.Empty<byte[]>(), new[] { root.RawData }));
        cases.Add(("missing", Array.Empty<byte>(), Array.Empty<byte[]>(), new[] { root.RawData }));
        var verify =
            typeof(SslStream)
                .Assembly.GetType("System.Net.CertificateValidation")
                .GetMethod("BuildChainAndVerifyProperties", BindingFlags.NonPublic | BindingFlags.Static);
        var rows = new List<object>();
        foreach (var c in cases)
            foreach (var host in new[] { "fixture.test", "other.test", "a.fixture.test", "127.0.0.1", "::1",
                                         "" })
                foreach (bool check in new[] { false, true })
                    foreach (bool system in new[] { false, true })
                    {
                        using var chain = new X509Chain();
                        chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
                        chain.ChainPolicy.RevocationFlag = X509RevocationFlag.ExcludeRoot;
                        chain.ChainPolicy.DisableCertificateDownloads = true;
                        chain.ChainPolicy.VerificationTime = At.UtcDateTime;
                        chain.ChainPolicy.ApplicationPolicy.Add(new Oid("1.3.6.1.5.5.7.3.1"));
                        foreach (var x in c.extra)
                            chain.ChainPolicy.ExtraStore.Add(new X509Certificate2(x));
                        if (!system)
                        {
                            chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
                            foreach (var x in c.roots)
                                chain.ChainPolicy.CustomTrustStore.Add(new X509Certificate2(x));
                        }
                        int expected = 1;
                        if (c.leaf.Length > 0)
                        {
                            using var leaf = new X509Certificate2(c.leaf);
                            expected = (int)(SslPolicyErrors)verify.Invoke(
                                null, new object[] { chain, leaf, check, false, host });
                        }
                        rows.Add(new { label = c.label, leaf = Hex(c.leaf),
                                       extra = c.extra.Select(Hex).ToArray(),
                                       roots = system ? null : c.roots.Select(Hex).ToArray(),
                                       time = At.UtcDateTime.ToFileTimeUtc(), checkName = check,
                                       host = Hex(Encoding.BigEndianUnicode.GetBytes(host)), expected });
                    }
        Console.WriteLine(JsonSerializer.Serialize(rows));
    }
}

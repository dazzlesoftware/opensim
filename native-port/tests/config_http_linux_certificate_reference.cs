using System;
using System.IO;
using System.Formats.Asn1;
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
                                 bool critical = false, X509Extension[] extensions = null)
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
        if (extensions != null)
            foreach (var extension in extensions)
                r.CertificateExtensions.Add(extension);
        if (issuer == null)
            return r.CreateSelfSigned(start ?? Start, end ?? End);
        return r.Create(issuer.SubjectName,
                        X509SignatureGenerator.CreateForRSA(issuerKey, RSASignaturePadding.Pkcs1),
                        start ?? Start, end ?? End, BitConverter.GetBytes(serial++));
    }
    static string Hex(byte[] x) => Convert.ToHexString(x).ToLowerInvariant();
    static byte[] Policies(params string[] values)
    {
        var w = new AsnWriter(AsnEncodingRules.DER);
        w.PushSequence();
        foreach (var value in values)
        {
            w.PushSequence();
            w.WriteObjectIdentifier(value);
            w.PopSequence();
        }
        w.PopSequence();
        return w.Encode();
    }
    static string Flags(IEnumerable<X509ChainStatus> flags)
    {
        var values = flags.Select(s => ((uint)s.Status).ToString()).ToArray();
        return values.Length == 0 ? "-" : string.Join(",", values);
    }
    sealed record Row(string Label, byte[] Leaf, byte[][] Extra, byte[][] Roots, byte[][] Issuers);
    static void Main()
    {
        using var root = Cert("Root", RootKey, null, null, true, null, "none", "none");
        using var userRoot = Cert("User Root", RootKey, null, null, true, null, "none", "none");
        using var wrongRoot = Cert("Wrong Root", RootKey, null, null, true, null, "none", "none");
        using var intermediate =
            Cert("Remote Intermediate", IntermediateKey, root, RootKey, true, 0, "none", "none");
        using var userIntermediate =
            Cert("User Intermediate", IntermediateKey, root, RootKey, true, 0, "none", "none");
        using var personal =
            Cert("Personal Intermediate", IntermediateKey, root, RootKey, true, 0, "none", "none");
        using var systemIntermediate =
            Cert("System Intermediate", IntermediateKey, root, RootKey, true, 0, "none", "none");
        using var noCa = Cert("Not CA", IntermediateKey, root, RootKey, false, null, "none", "none");
        using var root0 = Cert("Root Zero", RootKey, null, null, true, 0, "none", "none");
        using var intermediate0 =
            Cert("Intermediate Zero", IntermediateKey, root0, RootKey, true, 0, "none", "none");
        File.WriteAllText(Environment.GetEnvironmentVariable("SSL_CERT_FILE"),
                          root.ExportCertificatePem() + "\n" + systemIntermediate.ExportCertificatePem());
        void UserStore(string name, X509Certificate2 cert)
        {
            var dir = Path.Combine(Environment.GetEnvironmentVariable("HOME"), ".dotnet", "corefx",
                                   "cryptography", "x509stores", name);
            Directory.CreateDirectory(dir);
            File.WriteAllBytes(Path.Combine(dir, name + ".pfx"), cert.Export(X509ContentType.Pfx));
        }
        UserStore("root", userRoot);
        UserStore("ca", userIntermediate);
        UserStore("my", personal);
        var cases = new List<Row>();
        void Add(string label, X509Certificate2 leaf, byte[][] extra = null, byte[][] roots = null,
                 byte[][] issuers = null) =>
            cases.Add(new(label, leaf.RawData, extra ?? Array.Empty<byte[]>(),
                          roots ?? new[] { root.RawData }, issuers ?? Array.Empty<byte[]>()));
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
        Add("extra-intermediate", Cert("fixture.test", LeafKey, intermediate, IntermediateKey),
            new[] { intermediate.RawData });
        Add("missing-intermediate", Cert("fixture.test", LeafKey, intermediate, IntermediateKey));
        Add("duplicate-extra", Cert("fixture.test", LeafKey, intermediate, IntermediateKey),
            new[] { root.RawData, intermediate.RawData, intermediate.RawData });
        Add("user-ca-intermediate", Cert("fixture.test", LeafKey, userIntermediate, IntermediateKey));
        Add("user-my-intermediate", Cert("fixture.test", LeafKey, personal, IntermediateKey));
        Add("system-intermediate", Cert("fixture.test", LeafKey, systemIntermediate, IntermediateKey));
        Add("user-root", Cert("fixture.test", LeafKey, userRoot, RootKey), roots: new[] { userRoot.RawData });
        Add("nonself-custom-root", Cert("fixture.test", LeafKey, intermediate, IntermediateKey),
            roots: new[] { intermediate.RawData, root.RawData });
        Add("issuer-not-ca", Cert("fixture.test", LeafKey, noCa, IntermediateKey), new[] { noCa.RawData });
        Add("path-length", Cert("fixture.test", LeafKey, intermediate0, IntermediateKey),
            new[] { intermediate0.RawData }, new[] { root0.RawData });
        Add("wrong-root", Cert("fixture.test", LeafKey, root, RootKey), roots: new[] { wrongRoot.RawData });
        Add("empty-roots", Cert("fixture.test", LeafKey, root, RootKey), roots: Array.Empty<byte[]>());
        using var self = Cert("fixture.test", LeafKey, null, null);
        Add("self-leaf-trusted", self, roots: new[] { self.RawData });
        Add("self-leaf-untrusted", self);
        var damaged = cases[0].Leaf.ToArray();
        damaged[^1] ^= 1;
        cases.Add(new("bad-leaf-signature", damaged, Array.Empty<byte[]>(), new[] { root.RawData },
                      Array.Empty<byte[]>()));
        var damagedRoot = root.RawData.ToArray();
        damagedRoot[^1] ^= 1;
        Add("bad-root-signature", Cert("fixture.test", LeafKey, root, RootKey), roots: new[] { damagedRoot });
        Add("issuer-continuation", Cert("fixture.test", LeafKey, intermediate, IntermediateKey),
            issuers: new[] { intermediate.RawData });
        Add("wrong-then-correct-issuer", Cert("fixture.test", LeafKey, intermediate, IntermediateKey),
            issuers: new[] { wrongRoot.RawData, intermediate.RawData });
        using var grandparent =
            Cert("Remote Grandparent", IntermediateKey, root, RootKey, true, 2, "none", "none");
        using var parent =
            Cert("Remote Parent", IntermediateKey, grandparent, IntermediateKey, true, 0, "none", "none");
        Add("two-issuer-continuations", Cert("fixture.test", LeafKey, parent, IntermediateKey),
            issuers: new[] { parent.RawData, grandparent.RawData });
        foreach (var value in new[] { "1.3.6.1.5.5.7.3.1", "1.3.6.1.5.5.7.3.2", "2.5.29.32.0",
                                      "2.5.29.37.0" })
            Add("application-override-" + value,
                Cert("fixture.test", LeafKey, root, RootKey, eku: "client",
                     extensions: new[] { new X509Extension("1.3.6.1.4.1.311.21.10", Policies(value),
                                                           false) }));
        Add("application-masks-invalid-eku",
            Cert("fixture.test", LeafKey, root, RootKey, eku: "none",
                 extensions: new[] { new X509Extension("1.3.6.1.4.1.311.21.10", Policies("1.3.6.1.5.5.7.3.1"),
                                                       false),
                                     new X509Extension("2.5.29.37", new byte[] { 255 }, false) }));
        Add("invalid-application", Cert("fixture.test", LeafKey, root, RootKey,
                                        extensions: new[] { new X509Extension("1.3.6.1.4.1.311.21.10",
                                                                              new byte[] { 255 }, false) }));
        Add("certificate-policy",
            Cert("fixture.test", LeafKey, root, RootKey,
                 extensions: new[] { new X509Extension("2.5.29.32", Policies("1.2.3"), false) }));
        Add("invalid-certificate-policy",
            Cert("fixture.test", LeafKey, root, RootKey,
                 extensions: new[] { new X509Extension("2.5.29.32", new byte[] { 255 }, false) }));
        using var clientRoot = Cert("Client Root", RootKey, null, null, true, null, "client", "none");
        Add("root-client-eku", Cert("fixture.test", LeafKey, clientRoot, RootKey),
            roots: new[] { clientRoot.RawData });
        using var overrideRoot = Cert("Override Root", RootKey, null, null, true, null, "client", "none",
                                      extensions: new[] { new X509Extension(
                                          "1.3.6.1.4.1.311.21.10", Policies("1.3.6.1.5.5.7.3.1"), false) });
        Add("root-application-override", Cert("fixture.test", LeafKey, overrideRoot, RootKey),
            roots: new[] { overrideRoot.RawData });
        using var cycleStart = Cert("Cycle B", RootKey, null, null, true, null, "none", "none");
        using var cycleA = Cert("Cycle A", IntermediateKey, cycleStart, RootKey, true, null, "none", "none");
        using var cycleB = Cert("Cycle B", RootKey, cycleA, IntermediateKey, true, null, "none", "none");
        Add("cyclic", Cert("fixture.test", LeafKey, cycleA, IntermediateKey),
            new[] { cycleA.RawData, cycleB.RawData });
        cases.Add(new("missing", Array.Empty<byte>(), Array.Empty<byte[]>(), new[] { root.RawData },
                      Array.Empty<byte[]>()));
        var verify =
            typeof(SslStream)
                .Assembly.GetType("System.Net.Security.CertificateValidation")
                .GetMethod("BuildChainAndVerifyProperties", BindingFlags.NonPublic | BindingFlags.Static);
        var rows = new List<object>();
        foreach (var c in cases)
            foreach (var host in new[] { "fixture.test", "other.test", "a.fixture.test", "127.0.0.1", "::1",
                                         "", "-a.fixture.test" })
                foreach (bool check in new[] { false, true })
                    foreach (bool system in new[] { false, true })
                        foreach (bool policy in new[] { false, true })
                            foreach (bool certificatePolicy in new[] { false, true })
                            {
                                using var chain = new X509Chain();
                                chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
                                chain.ChainPolicy.RevocationFlag = X509RevocationFlag.ExcludeRoot;
                                chain.ChainPolicy.DisableCertificateDownloads = true;
                                chain.ChainPolicy.VerificationTime = At.UtcDateTime;
                                if (policy)
                                    chain.ChainPolicy.ApplicationPolicy.Add(new Oid("1.3.6.1.5.5.7.3.1"));
                                if (certificatePolicy)
                                    chain.ChainPolicy.CertificatePolicy.Add(new Oid("1.2.3"));
                                foreach (var x in c.Extra)
                                    chain.ChainPolicy.ExtraStore.Add(new X509Certificate2(x));
                                if (!system)
                                {
                                    chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
                                    foreach (var x in c.Roots)
                                        chain.ChainPolicy.CustomTrustStore.Add(new X509Certificate2(x));
                                }
                                string expected;
                                try
                                {
                                    int errors = 1;
                                    var trace = new List<string>();
                                    if (c.Leaf.Length > 0)
                                    {
                                        using var leaf = new X509Certificate2(c.Leaf);
                                        foreach (var issuer in c.Issuers)
                                        {
                                            chain.Build(leaf);
                                            if (!chain.ChainStatus.Any(
                                                    s => s.Status == X509ChainStatusFlags.PartialChain))
                                                throw new Exception("Expected incomplete source path");
                                            trace.Add(Hex(chain.ChainElements[^1].Certificate.RawData));
                                            chain.ChainPolicy.ExtraStore.Add(new X509Certificate2(issuer));
                                        }
                                        errors = (int)(SslPolicyErrors)verify.Invoke(
                                            null, new object[] { chain, leaf, check, false, host });
                                    }
                                    expected = errors + " " + Flags(chain.ChainStatus) +
                                               " T:" + (trace.Count == 0 ? "-" : string.Join(",", trace));
                                    foreach (var element in chain.ChainElements)
                                        expected += " " + Hex(element.Certificate.RawData) + ":" +
                                                    Flags(element.ChainElementStatus);
                                }
                                catch (Exception e)
                                {
                                    while (e is TargetInvocationException && e.InnerException != null)
                                        e = e.InnerException;
                                    expected = "E " + e.GetType().FullName + " " +
                                               unchecked((uint)e.HResult) + " " +
                                               Hex(Encoding.BigEndianUnicode.GetBytes(e.Message));
                                }
                                rows.Add(
                                    new { label = c.Label, leaf = Hex(c.Leaf),
                                          extra = c.Extra.Select(Hex).ToArray(),
                                          trusted = new[] { Hex(root.RawData), Hex(userRoot.RawData) },
                                          userIntermediates =
                                              new[] { Hex(userIntermediate.RawData), Hex(personal.RawData) },
                                          systemIntermediates = new[] { Hex(systemIntermediate.RawData) },
                                          roots = system ? null : c.Roots.Select(Hex).ToArray(),
                                          issuers = c.Issuers.Select(Hex).ToArray(),
                                          time = At.ToUnixTimeSeconds(), checkName = check,
                                          application =
                                              policy ? new[] { "1.3.6.1.5.5.7.3.1" } : Array.Empty<string>(),
                                          certificate =
                                              certificatePolicy ? new[] { "1.2.3" } : Array.Empty<string>(),
                                          host = Hex(Encoding.BigEndianUnicode.GetBytes(host)), expected });
                            }
        Console.WriteLine(JsonSerializer.Serialize(rows));
    }
}

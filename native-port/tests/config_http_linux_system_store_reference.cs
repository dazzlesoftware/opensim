using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using System.Formats.Asn1;
using System.Diagnostics;
class Program
{
    [DllImport("libcrypto.so.3")]
    static extern int OPENSSL_sk_num(IntPtr stack);
    [DllImport("libcrypto.so.3")]
    static extern IntPtr OPENSSL_sk_value(IntPtr stack, int index);
    static X500DistinguishedName Name(int tag, string value)
    {
        var w = new AsnWriter(AsnEncodingRules.DER);
        w.PushSequence();
        w.PushSetOf();
        w.PushSequence();
        w.WriteObjectIdentifier("2.5.4.3");
        w.WriteCharacterString((UniversalTagNumber)tag, value);
        w.PopSequence();
        w.PopSetOf();
        w.PopSequence();
        return new X500DistinguishedName(w.Encode());
    }
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--cache")
        {
            var type =
                typeof(X509Certificate2)
                    .Assembly.GetType(
                        "System.Security.Cryptography.X509Certificates.OpenSslCachedSystemStoreProvider",
                        true);
            var get = type.GetMethod("GetCollections", BindingFlags.Static | BindingFlags.NonPublic);
            var watch =
                (Stopwatch)type.GetField("s_recheckStopwatch", BindingFlags.Static | BindingFlags.NonPublic)
                    .GetValue(null);
            object previous = null;
            long loaded = 0;
            string line;
            while ((line = Console.ReadLine()) != null)
            {
                long now = long.Parse(line);
                watch.Stop();
                typeof(Stopwatch)
                    .GetField("_elapsed", BindingFlags.Instance | BindingFlags.NonPublic)
                    .SetValue(watch, (now - loaded) * Stopwatch.Frequency / 1000);
                var result = get.Invoke(null, null);
                bool same = ReferenceEquals(previous, result);
                Console.Write(same ? "S" : "N");
                foreach (var field in new[] { ("Item1", "r"), ("Item2", "i") })
                {
                    var stack = (SafeHandle)result.GetType().GetProperty(field.Item1).GetValue(result);
                    for (int i = 0; i < OPENSSL_sk_num(stack.DangerousGetHandle()); i++)
                    {
                        using var cert =
                            new X509Certificate2(OPENSSL_sk_value(stack.DangerousGetHandle(), i));
                        Console.Write(" " + field.Item2 +
                                      Convert.ToHexString(cert.RawData).ToLowerInvariant());
                    }
                }
                Console.WriteLine();
                if (!same)
                    loaded = now;
                previous = result;
            }
            return;
        }
        if (args.Length == 0)
        {
            var type =
                typeof(X509Certificate2)
                    .Assembly.GetType(
                        "System.Security.Cryptography.X509Certificates.OpenSslCachedSystemStoreProvider",
                        true);
            var result = type.GetMethod("GetCollections", BindingFlags.Static | BindingFlags.NonPublic)
                             .Invoke(null, null);
            foreach (var field in new[] { ("Item1", "r"), ("Item2", "i") })
            {
                var stack = (SafeHandle)result.GetType().GetProperty(field.Item1).GetValue(result);
                for (int i = 0; i < OPENSSL_sk_num(stack.DangerousGetHandle()); i++)
                {
                    using var cert = new X509Certificate2(OPENSSL_sk_value(stack.DangerousGetHandle(), i));
                    Console.WriteLine(field.Item2 + " " +
                                      Convert.ToHexString(cert.RawData).ToLowerInvariant());
                }
            }
            return;
        }
        var work = Path.GetFullPath(args[0]);
        Directory.CreateDirectory(work);
        using var key = RSA.Create(2048);
        int serial = 1;
        X509Certificate2 Cert(X500DistinguishedName subject, X500DistinguishedName issuer)
        {
            var r = new CertificateRequest(subject, key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
            return r.Create(issuer, X509SignatureGenerator.CreateForRSA(key, RSASignaturePadding.Pkcs1),
                            DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1),
                            BitConverter.GetBytes(serial++));
        }
        using var root = Cert(Name(12, "Root"), Name(12, "Root"));
        using var intermediate = Cert(Name(12, "Intermediate"), Name(12, "Root"));
        using var encodingRoot = Cert(Name(12, "Same"), Name(19, "Same"));
        using var nulRoot = Cert(Name(12, "Trailing\0"), Name(12, "Trailing"));
        using var caseIntermediate = Cert(Name(12, "CASE"), Name(12, "case"));
        using var sameNamesDifferentCert = Cert(Name(12, "Root"), Name(12, "Root"));
        var one = root.ExportCertificatePem() + "\n";
        var other = intermediate.ExportCertificatePem() + "\n";
        var files = new Dictionary<string, byte[]> {
            ["empty"] = Array.Empty<byte>(),
            ["root"] = System.Text.Encoding.ASCII.GetBytes(one),
            ["intermediate"] = System.Text.Encoding.ASCII.GetBytes(other),
            ["bundle"] = System.Text.Encoding.ASCII.GetBytes(one + other + one),
            ["der"] = root.RawData,
            ["derpair"] = root.RawData.Concat(intermediate.RawData).ToArray(),
            ["garbage"] = new byte[] { 1, 2, 3, 4 },
            ["pemaftergarbage"] = System.Text.Encoding.ASCII.GetBytes("junk\n" + one),
            ["derthenpem"] = root.RawData.Concat(System.Text.Encoding.ASCII.GetBytes(other)).ToArray(),
            ["pemthender"] = System.Text.Encoding.ASCII.GetBytes(one).Concat(intermediate.RawData).ToArray(),
            ["formattedroot"] =
                System.Text.Encoding.ASCII.GetBytes(encodingRoot.ExportCertificatePem() + "\n"),
            ["nulroot"] = System.Text.Encoding.ASCII.GetBytes(nulRoot.ExportCertificatePem() + "\n"),
            ["caseintermediate"] =
                System.Text.Encoding.ASCII.GetBytes(caseIntermediate.ExportCertificatePem() + "\n"),
            ["distinctsameissuer"] = System.Text.Encoding.ASCII.GetBytes(
                one + sameNamesDifferentCert.ExportCertificatePem() + "\n"),
            ["keythenpem"] = System.Text.Encoding.ASCII.GetBytes(key.ExportPkcs8PrivateKeyPem() + "\n" + one),
            ["trustedlabel"] =
                System.Text.Encoding.ASCII.GetBytes(one.Replace("CERTIFICATE", "TRUSTED CERTIFICATE")),
            ["malformedpemfirst"] = System.Text.Encoding.ASCII.GetBytes(
                "-----BEGIN CERTIFICATE-----\nAQIDBA==\n-----END CERTIFICATE-----\n" + one)
        };
        foreach (var file in files)
            File.WriteAllBytes(Path.Combine(work, file.Key), file.Value);
        var directory = Path.Combine(work, "certs");
        Directory.CreateDirectory(directory);
        foreach (var file in Directory.EnumerateFiles(directory))
            File.Delete(file);
        File.WriteAllText(Path.Combine(directory, "one.crt"), one);
        File.WriteAllText(Path.Combine(directory, "two.any"), other);
        File.WriteAllText(Path.Combine(directory, "duplicate.crt"), one);
        Directory.CreateDirectory(Path.Combine(directory, "nested"));
        File.WriteAllText(Path.Combine(directory, "nested/ignored.crt"),
                          caseIntermediate.ExportCertificatePem());
        File.CreateSymbolicLink(Path.Combine(directory, "link.crt"), Path.Combine(work, "formattedroot"));
        File.CreateSymbolicLink(Path.Combine(directory, "broken.crt"), Path.Combine(work, "absent"));
        var rows = new List<object>();
        foreach (var file in files.Keys)
            rows.Add(new { label = file, file = Path.Combine(work, file), dirs = "" });
        rows.Add(new { label = "emptyenv", file = "", dirs = "" });
        rows.Add(new { label = "missing", file = Path.Combine(work, "absent"),
                       dirs = Path.Combine(work, "missing") });
        rows.Add(new { label = "dir", file = "", dirs = directory });
        rows.Add(new { label = "fileanddir", file = Path.Combine(work, "bundle"), dirs = directory });
        rows.Add(new { label = "duplicatepaths", file = "",
                       dirs = directory + ":" + directory + "::" + Path.Combine(directory, ".") + ":" });
        rows.Add(new { label = "filenotdir", file = directory, dirs = Path.Combine(work, "root") });
        rows.Add(new { label = "relative",
                       file = Path.GetRelativePath(Environment.CurrentDirectory, Path.Combine(work, "root")),
                       dirs = Path.GetRelativePath(Environment.CurrentDirectory, directory) });
        rows.Add(new { label = "native-defaults", file = (string)null, dirs = (string)null });
        Console.WriteLine(JsonSerializer.Serialize(rows));
    }
}

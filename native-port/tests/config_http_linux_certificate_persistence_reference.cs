using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Collections.Generic;
class Program
{
    [DllImport("libcrypto.so.3")]
    static extern int OPENSSL_sk_num(IntPtr value);
    static void Main(string[] args)
    {
        if (args[0] == "--prepare")
        {
            using var key = RSA.Create(2048);
            foreach (var name in new[] { "a", "b", "c" })
            {
                var req = new CertificateRequest("CN=" + name, key, HashAlgorithmName.SHA256,
                                                 RSASignaturePadding.Pkcs1);
                using var cert =
                    req.Create(new X500DistinguishedName("CN=Issuer"),
                               X509SignatureGenerator.CreateForRSA(key, RSASignaturePadding.Pkcs1),
                               new DateTimeOffset(2020, 1, 1, 0, 0, 0, TimeSpan.Zero),
                               new DateTimeOffset(2040, 1, 1, 0, 0, 0, TimeSpan.Zero),
                               new byte[] { (byte)(name == "c" ? 2 : 1) });
                File.WriteAllBytes(Path.Combine(args[1], name + ".der"), cert.RawData);
                File.WriteAllBytes(Path.Combine(args[1], name + ".pfx"), cert.Export(X509ContentType.Pfx));
                if (name == "a")
                {
                    using var keyed = cert.CopyWithPrivateKey(key);
                    File.WriteAllBytes(Path.Combine(args[1], "keyed.pfx"), keyed.Export(X509ContentType.Pfx));
                }
            }
            using var a = new X509Certificate2(File.ReadAllBytes(Path.Combine(args[1], "a.der")));
            using var c = new X509Certificate2(File.ReadAllBytes(Path.Combine(args[1], "c.der")));
            File.WriteAllBytes(Path.Combine(args[1], "ac.pfx"),
                               new X509Certificate2Collection(new[] { a, c }).Export(X509ContentType.Pfx));
            File.WriteAllBytes(Path.Combine(args[1], "ca.pfx"),
                               new X509Certificate2Collection(new[] { c, a }).Export(X509ContentType.Pfx));
            using var ak = a.CopyWithPrivateKey(key);
            using var ck = c.CopyWithPrivateKey(key);
            File.WriteAllBytes(Path.Combine(args[1], "ac-keyed.pfx"),
                               new X509Certificate2Collection(new[] { ak, ck }).Export(X509ContentType.Pfx));
            File.WriteAllBytes(Path.Combine(args[1], "ca-keyed.pfx"),
                               new X509Certificate2Collection(new[] { ck, ak }).Export(X509ContentType.Pfx));
            File.WriteAllBytes(Path.Combine(args[1], "passworded.pfx"),
                               a.Export(X509ContentType.Pfx, "secret"));
            return;
        }
        var asm = typeof(X509Certificate2).Assembly;
        var processor =
            asm.GetType("System.Security.Cryptography.X509Certificates.OpenSslX509ChainProcessor", true);
        var provider = asm.GetType(
            "System.Security.Cryptography.X509Certificates.OpenSslCachedDirectoryStoreProvider", true);
        var get = provider.GetMethod("GetNativeCollection", BindingFlags.Instance | BindingFlags.NonPublic);
        var stores =
            new[] { "s_userRootStore", "s_userIntermediateStore", "s_userPersonalStore" }
                .Select(
                    name =>
                        processor.GetField(name, BindingFlags.Static | BindingFlags.NonPublic).GetValue(null))
                .ToArray();
        var before = stores.Select(store => (SafeHandle)get.Invoke(store, null)).ToArray();
        var counts = before.Select(x => OPENSSL_sk_num(x.DangerousGetHandle())).ToArray();
        var downloaded = new List<X509Certificate2>();
        try
        {
            for (int i = 1; i < args.Length; i++)
                downloaded.Add(new X509Certificate2(File.ReadAllBytes(args[i])));
            if (int.Parse(args[0]) == 0)
            {
                var save = asm.GetType("System.Security.Cryptography.X509Certificates.ChainPal", true)
                               .GetMethod("SaveIntermediateCertificates",
                                          BindingFlags.Static | BindingFlags.NonPublic);
                save.Invoke(null, new object[] { downloaded });
            }
            var after = stores.Select(store => (SafeHandle)get.Invoke(store, null)).ToArray();
            Console.WriteLine(
                string.Join(",", counts) + " " +
                string.Join(",", after.Select(x => OPENSSL_sk_num(x.DangerousGetHandle()))) + " " +
                string.Join(",", before.Select((x, i) => ReferenceEquals(x, after[i]) ? "0" : "1")));
        }
        finally
        {
            foreach (var cert in downloaded)
                cert.Dispose();
        }
    }
}

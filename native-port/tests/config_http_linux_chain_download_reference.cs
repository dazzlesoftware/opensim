using System;
using System.IO;
using System.Text.Json;
using System.Linq;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Collections.Generic;
class Program
{
    sealed record Reply(string uri, string der, int delay = 0);
    sealed record Row(string label, string leaf, string[] roots, string[] extra, string[] manual, int mode,
                      int timeout, Reply[] replies);
    static string Hex(byte[] x) => Convert.ToHexString(x).ToLowerInvariant();
    static string Flags(IEnumerable<X509ChainStatus> values)
    {
        var s = string.Join(",", values.Select(x => ((uint)x.Status).ToString()));
        return s.Length == 0 ? "-" : s;
    }
    static void Main(string[] args)
    {
        var row = JsonSerializer.Deserialize<Row[]>(File.ReadAllText(args[0]))[int.Parse(args[1])];
        int mode = int.Parse(args[2]);
        using var leaf = new X509Certificate2(Convert.FromHexString(row.leaf));
        for (int repeat = 0; repeat < 2; repeat++)
        {
            using var chain = new X509Chain();
            var p = chain.ChainPolicy;
            p.TrustMode = X509ChainTrustMode.CustomRootTrust;
            foreach (var der in row.roots)
                p.CustomTrustStore.Add(new X509Certificate2(Convert.FromHexString(der)));
            foreach (var der in row.extra.Concat(row.manual))
                p.ExtraStore.Add(new X509Certificate2(Convert.FromHexString(der)));
            p.RevocationMode = X509RevocationMode.NoCheck;
            p.VerificationTime = new DateTime(mode == 2 ? 2050 : 2030, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            p.UrlRetrievalTimeout = TimeSpan.FromTicks(long.Parse(args[3]));
            p.DisableCertificateDownloads = mode == 3;
            if (mode == 1)
                p.CertificatePolicy.Add(new Oid("1.2.3.4.5"));
            chain.Build(leaf);
            Console.Write(Flags(chain.ChainStatus));
            foreach (var element in chain.ChainElements)
                Console.Write(" " + Hex(element.Certificate.RawData) + ":" +
                              Flags(element.ChainElementStatus));
            Console.WriteLine();
            foreach (var c in p.CustomTrustStore)
                c.Dispose();
            foreach (var c in p.ExtraStore)
                c.Dispose();
        }
    }
}

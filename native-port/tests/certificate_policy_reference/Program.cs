using System;
using System.Collections.Generic;
using System.Globalization;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Collections;
using System.Reflection;
class Program
{
    static byte[] Bytes(string value) => value == "-"   ? null
                                         : value == "_" ? Array.Empty<byte>()
                                                        : Convert.FromHexString(value);
    static Oid Query(string value)
    {
        var oid = new Oid();
        oid.Value = value == "-" ? null : value == "_" ? "" : value;
        return oid;
    }
    static string Hex(string value) =>
        Convert.ToHexString(Encoding.BigEndianUnicode.GetBytes(value)).ToLowerInvariant();
    static readonly Dictionary<string, object> certificates = new();
    static RSA key;
    static Type runtimeChain, runtimeCertificate;
    static object RuntimeCertificate(PolicyData data, string id)
    {
        if (certificates.TryGetValue(id, out var existing))
            return existing;
        key ??= RSA.Create(2048);
        var request = new CertificateRequest("CN=Policy fixture", key, HashAlgorithmName.SHA256,
                                             RSASignaturePadding.Pkcs1);
        var values =
            new[] { data.ApplicationCertPolicies, data.CertPolicies,     data.CertPolicyMappings,
                    data.CertPolicyConstraints,   data.EnhancedKeyUsage, data.InhibitAnyPolicyExtension };
        var ids = new[] {
            "1.3.6.1.4.1.311.21.10", "2.5.29.32", "2.5.29.33", "2.5.29.36", "2.5.29.37", "2.5.29.54"
        };
        for (int i = 0; i < values.Length; i++)
            if (values[i] != null)
                request.CertificateExtensions.Add(new X509Extension(ids[i], values[i], false));
        var cert =
            request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
        certificates.Add(id, cert);
        return cert;
    }
    static void Main(string[] args)
    {
        bool runtime = args.Length > 0 && args[0] == "--runtime";
        if (runtime)
        {
            var assembly = typeof(RSA).Assembly;
            runtimeChain = assembly.GetType(
                "System.Security.Cryptography.X509Certificates.CertificatePolicyChain", true);
            runtimeCertificate =
                assembly.GetType("System.Security.Cryptography.X509Certificates.X509Certificate2", true);
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                var parts = line.Split(' ');
                int at = 0, count = int.Parse(parts[at++]);
                var chain = new List<X509Certificate2>();
                IList real =
                    runtime
                        ? (IList)Activator.CreateInstance(typeof(List<>).MakeGenericType(runtimeCertificate))
                        : null;
                for (int i = 0; i < count; i++)
                {
                    int begin = at;
                    var data = new PolicyData { ApplicationCertPolicies = Bytes(parts[at++]),
                                                CertPolicies = Bytes(parts[at++]),
                                                CertPolicyMappings = Bytes(parts[at++]),
                                                CertPolicyConstraints = Bytes(parts[at++]),
                                                EnhancedKeyUsage = Bytes(parts[at++]),
                                                InhibitAnyPolicyExtension = Bytes(parts[at++]) };
                    if (runtime)
                        real.Add(RuntimeCertificate(data, string.Join(" ", parts[begin..at])));
                    else
                        chain.Add(new X509Certificate2(data));
                }
                object processor = runtime ? Activator.CreateInstance(runtimeChain, new object[] { real })
                                           : new CertificatePolicyChain(chain);
                var mode = parts[at++];
                int n = int.Parse(parts[at++]);
                var queries = new OidCollection();
                for (int i = 0; i < n; i++)
                    queries.Add(Query(parts[at++]));
                bool result;
                if (runtime)
                {
                    string method =
                        mode is "A" or "a" ? "MatchesApplicationPolicies" : "MatchesCertificatePolicies";
                    var single = char.IsUpper(mode[0]);
                    result = (bool)runtimeChain
                                 .GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic, null,
                                            new[] { single ? typeof(Oid) : typeof(OidCollection) }, null)
                                 .Invoke(processor, new object[] { single ? queries[0] : queries });
                }
                else
                {
                    var typed = (CertificatePolicyChain)processor;
                    result = mode switch { "A" => typed.MatchesApplicationPolicies(queries[0]),
                                           "C" => typed.MatchesCertificatePolicies(queries[0]),
                                           "a" => typed.MatchesApplicationPolicies(queries),
                                           "c" => typed.MatchesCertificatePolicies(queries),
                                           _ => throw new Exception("Invalid probe mode") };
                }
                Console.WriteLine(result ? "1" : "0");
            }
            catch (Exception e)
            {
                while (e is TargetInvocationException && e.InnerException != null)
                    e = e.InnerException;
                Console.WriteLine("E " + e.GetType().FullName + " " +
                                  unchecked((uint)e.HResult).ToString(CultureInfo.InvariantCulture) + " " +
                                  Hex(e.Message));
            }
        }
    }
}

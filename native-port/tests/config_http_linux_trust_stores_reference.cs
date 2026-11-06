using System;
using System.IO;
using System.Reflection;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using System.Collections.Generic;
class Program
{
    static readonly Assembly Crypto = typeof(X509Certificate2).Assembly;
    static string Hex(string value)
    {
        if (string.IsNullOrEmpty(value))
            return "_";
        var result = "";
        foreach (var c in value)
            result += ((ushort)c).ToString("x4");
        return result;
    }
    static void Error(Exception e)
    {
        while (e is TargetInvocationException)
            e = e.InnerException;
        Console.Write("E " + e.GetType().FullName + " " + unchecked((uint)e.HResult) + " " + Hex(e.Message));
        if (e.InnerException != null)
        {
            Console.Write(" | ");
            Error(e.InnerException);
        }
    }
    static void Main(string[] args)
    {
        try
        {
            if (args[0] == "--resources")
            {
                var sr = Crypto.GetType("System.SR", true);
                var values = new Dictionary<string, string>();
                foreach (var name in new[] { "PersistedFiles_NoHomeDirectory",
                                             "Cryptography_Unix_X509_DisallowedStoreNotEmpty" })
                    values[name] = (string)sr.GetProperty(name, BindingFlags.Static | BindingFlags.NonPublic)
                                       .GetValue(null);
                Console.WriteLine(JsonSerializer.Serialize(values));
                return;
            }
            if (args[0] == "--paths")
            {
                var persisted = Crypto.GetType("System.IO.PersistedFiles", true);
                void Paths()
                {
                    var home =
                        (string)persisted
                            .GetMethod("GetHomeDirectory", BindingFlags.Static | BindingFlags.NonPublic)
                            .Invoke(null, null);
                    var root = (string)persisted
                                   .GetMethod("GetUserFeatureDirectory",
                                              BindingFlags.Static | BindingFlags.NonPublic)
                                   .Invoke(null, new object[] { "cryptography", "x509stores" });
                    Console.WriteLine("H " + Hex(home) + " R " + Hex(root));
                }
                Paths();
                if (args.Length > 1)
                {
                    Environment.SetEnvironmentVariable("HOME", args[1]);
                    Paths();
                }
            }
            else
            {
                using var store = new X509Store(StoreName.Disallowed, StoreLocation.CurrentUser);
                store.Open(OpenFlags.ReadOnly);
                Console.WriteLine("OK");
            }
        }
        catch (Exception e)
        {
            Error(e);
            Console.WriteLine();
        }
    }
}

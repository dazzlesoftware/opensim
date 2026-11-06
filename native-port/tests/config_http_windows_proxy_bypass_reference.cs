using System;
using System.Net;
using System.Net.Http;
using System.Net.NetworkInformation;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Collections.Generic;
using System.Linq;
class Reference
{
    static string? Decode(string s)
    {
        if (s == "null")
            return null;
        if (s == "-")
            return "";
        var chars = new char[s.Length / 4];
        for (int i = 0; i < chars.Length; i++)
            chars[i] = (char)Convert.ToUInt16(s.Substring(i * 4, 4), 16);
        return new string(chars);
    }
    static string Hex(string s)
    {
        if (s.Length == 0)
            return "-";
        var b = new System.Text.StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static void Main(string[] args)
    {
        if (args.Length != 0)
        {
            foreach (var ip in NetworkInterface.GetAllNetworkInterfaces()
                         .SelectMany(n => n.GetIPProperties().UnicastAddresses)
                         .Select(a => a.Address.ToString())
                         .OrderBy(s => s, StringComparer.Ordinal))
                Console.WriteLine(ip);
            return;
        }
        var assembly = typeof(HttpClient).Assembly;
        var simple = assembly.GetType("System.Text.SimpleRegex", true)!.GetMethod(
            "IsMatchWithStarWildcard", BindingFlags.Public | BindingFlags.Static)!;
        var wrapper = new DynamicMethod("Wildcard", typeof(bool), new[] { typeof(string), typeof(string) },
                                        simple.Module, true);
        var il = wrapper.GetILGenerator();
        var span = typeof(MemoryExtensions).GetMethod("AsSpan", new[] { typeof(string) })!;
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Call, span);
        il.Emit(OpCodes.Ldarg_1);
        il.Emit(OpCodes.Call, span);
        il.Emit(OpCodes.Call, simple);
        il.Emit(OpCodes.Ret);var match=(Func<string?,string?,bool>)wrapper.CreateDelegate(typeof(Func<string?,string?,bool>));
        var proxyType = assembly.GetType("System.Net.Http.HttpWindowsProxy", true)!;
        var helperType = assembly.GetType("System.Net.Http.WinInetProxyHelper", true)!;
        var constructor = proxyType.GetConstructors(BindingFlags.NonPublic | BindingFlags.Instance).Single();
        var proxies = new Dictionary<string, IWebProxy>();
        string? row;
        while ((row = Console.ReadLine()) != null)
        {
            var pieces = row.Split(' ');
            try
            {
                if (pieces[0] == "W")
                    Console.Write(match(Decode(pieces[1]), Decode(pieces[2])) ? "1" : "0");
                else
                {
                    Uri? uri =
                        pieces[1] == "null" ? null : new Uri(Decode(pieces[1])!, UriKind.RelativeOrAbsolute);
                    if (pieces[0] == "I")
                        Console.Write((int)uri!.HostNameType + ":" + (uri.IsLoopback ? "1" : "0"));
                    else
                    {
                        if (!proxies.TryGetValue(pieces[2], out var proxy))
                        {
                            var helper = RuntimeHelpers.GetUninitializedObject(helperType);
                            helperType.GetField("_proxy", BindingFlags.NonPublic | BindingFlags.Instance)!
                                .SetValue(helper, "http=insecure:8080;https=secure:8443");
                            helperType
                                .GetField("_proxyBypass",
                                          BindingFlags.NonPublic |
                                              BindingFlags.Instance)!.SetValue(helper, Decode(pieces[2]));
                            proxy = (IWebProxy)constructor.Invoke(new object?[] { helper, null });
                            if ((bool)proxyType
                                    .GetField("_bypassLocal", BindingFlags.NonPublic |
                                                                  BindingFlags.Instance)!.GetValue(proxy)!)
                                proxyType
                                    .GetField("_localIp", BindingFlags.NonPublic | BindingFlags.Instance)!
                                    .SetValue(proxy, new List<IPAddress> { IPAddress.Parse("192.0.2.10"),
                                                                           IPAddress.Parse("2001:db8::10"),
                                                                           IPAddress.Parse("fe80::1%7") });
                            proxies[pieces[2]] = proxy;
                        }
                        var result = proxy.GetProxy(uri!);
                        Console.Write(
                            (result == null ? "1" : "0") + ":" +
                            ((bool)proxyType
                                     .GetField("_bypassLocal", BindingFlags.NonPublic |
                                                                   BindingFlags.Instance)!.GetValue(proxy)!
                                 ? "1"
                                 : "0") +
                            ":");
                        var patterns =
                            (List<string>?)
                                proxyType.GetField("_bypass", BindingFlags.NonPublic |
                                                                  BindingFlags.Instance)!.GetValue(proxy);
                        if (patterns != null)
                            foreach (var pattern in patterns)
                                Console.Write(Hex(pattern) + ",");
                    }
                }
            }
            catch (Exception e)
            {
                while (e is TargetInvocationException && e.InnerException != null)
                    e = e.InnerException;
                Console.Write("E:" + e.GetType().FullName + ":" + e.HResult + ":" + Hex(e.Message));
            }
            Console.WriteLine();
        }
    }
}

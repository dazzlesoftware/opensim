using System;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Linq;
class Reference
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    delegate void Configure(int a, int b, int c, [MarshalAs(UnmanagedType.LPWStr)] string? proxy,
                            [MarshalAs(UnmanagedType.LPWStr)] string? bypass);
    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    delegate void Settings(int success, int detect, [MarshalAs(UnmanagedType.LPWStr)] string? url,
                           [MarshalAs(UnmanagedType.LPWStr)] string? proxy,
                           [MarshalAs(UnmanagedType.LPWStr)] string? bypass, int valid);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    delegate int Count();
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
    static string Hex(string? s)
    {
        if (s == null)
            return "null";
        if (s.Length == 0)
            return "-";
        var b = new System.Text.StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static void Main(string[] args)
    {
        if (args[0] == "--default")
        {
            var proxy = HttpClient.DefaultProxy;
            string kind = proxy.GetType().Name switch { "HttpWindowsProxy" => "windows",
                                                        "HttpEnvironmentProxy" => "environment",
                                                        "HttpNoProxy" => "none",
                                                        _ => "unknown" };
            Console.WriteLine(kind + ":" + (proxy.Credentials != null ? "1" : "0"));
            return;
        }
        var assembly = typeof(HttpClient).Assembly;
        var type = assembly.GetType("System.Net.Http.HttpWindowsProxy", true)!;
        var create = type.GetMethod("TryCreate")!;
        if (args[0] == "--native")
        {
            var values = new object?[] { null };
            Console.WriteLine((bool)create.Invoke(null, values)!? "1" : "0");
            (values[0] as IDisposable)?.Dispose();
            return;
        }
        var dll = NativeLibrary.Load(args[0]);
        NativeLibrary.SetDllImportResolver(
            assembly, (name, owner, path) =>
                          name.Equals("winhttp.dll", StringComparison.OrdinalIgnoreCase) ? dll : IntPtr.Zero);
        var configure =
            Marshal.GetDelegateForFunctionPointer<Configure>(NativeLibrary.GetExport(dll, "Configure"));
        var settings = Marshal.GetDelegateForFunctionPointer<Settings>(
            NativeLibrary.GetExport(dll, "ConfigureSettings"));
        var opens = Marshal.GetDelegateForFunctionPointer<Count>(NativeLibrary.GetExport(dll, "OpenCount"));
        var closes = Marshal.GetDelegateForFunctionPointer<Count>(NativeLibrary.GetExport(dll, "CloseCount"));
        var calls = Marshal.GetDelegateForFunctionPointer<Count>(NativeLibrary.GetExport(dll, "CallCount"));
        var multi = type.GetMethod("GetMultiProxy")!;
        var mt = multi.ReturnType;
        var read = mt.GetMethod("ReadNext")!;
        var index = mt.GetField("_currentIndex", BindingFlags.NonPublic | BindingFlags.Instance)!;
        string? row;
        while ((row = Console.ReadLine()) != null)
        {
            var p = row.Split(' ');
            settings(int.Parse(p[1]), int.Parse(p[2]), Decode(p[3]), Decode(p[4]), Decode(p[5]),
                     int.Parse(p[6]));
            var errors = p[7].Split(',');
            int a = int.Parse(errors[0]), b = errors.Length > 1 ? int.Parse(errors[1]) : a,
                c = errors.Length > 2 ? int.Parse(errors[2]) : b;
            configure(a, b, c, Decode(p[8]), Decode(p[9]));
            var values = new object?[] { null };
            bool found = (bool)create.Invoke(null, values)!;
            var proxy = (IWebProxy?)values[0];
            if (!found)
                Console.Write("NONE");
            else
            {
                if ((bool)type.GetField("_bypassLocal",
                                        BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(proxy)!)
                    type.GetField("_localIp", BindingFlags.NonPublic | BindingFlags.Instance)!.SetValue(
                        proxy, new List<IPAddress> { IPAddress.Parse("192.0.2.10") });
                Uri? uri = p[10] == "null" ? null : new Uri(Decode(p[10])!, UriKind.RelativeOrAbsolute);
                bool initial = proxy!.Credentials == null;
                var credential = CredentialCache.DefaultNetworkCredentials;
                proxy.Credentials = credential;
                if (p[0] == "D")
                    ((IDisposable)proxy).Dispose();
                Console.Write((proxy.IsBypassed(uri!) ? "1" : "0") + ":" + (initial ? "1" : "0") + "|");
                for (int query = 0; query < 2; query++)
                {
                    if (query != 0)
                        Console.Write("/");
                    try
                    {
                        if (p[0] == "M")
                        {
                            var list = multi.Invoke(proxy, new object?[] { uri })!;
                            for (int i = 0; i < 100; i++)
                            {
                                var output = new object?[] { null, false };
                                bool next = (bool)read.Invoke(list, output)!;
                                if (i != 0)
                                    Console.Write(",");
                                Console.Write((next ? "1" : "0") + ":" + ((bool)output[1]!? "1" : "0") + ":" +
                                              index.GetValue(list) + ":" +
                                              Hex((output[0] as Uri)?.AbsoluteUri));
                                if (!next)
                                    break;
                            }
                        }
                        else
                            Console.Write(Hex(proxy.GetProxy(uri!)?.AbsoluteUri));
                    }
                    catch (Exception e)
                    {
                        while (e is TargetInvocationException && e.InnerException != null)
                            e = e.InnerException;
                        Console.Write("E:" + e.GetType().FullName + ":" + e.HResult + ":" + Hex(e.Message));
                    }
                }
                bool retained = ReferenceEquals(proxy.Credentials, credential);
                proxy.Credentials = null;
                ((IDisposable)proxy).Dispose();
                Console.Write("|" + (retained ? "1" : "0") + ":" + (proxy.Credentials == null ? "1" : "0"));
            }
            Console.WriteLine("#" + opens() + ":" + closes() + ":" + calls());
        }
    }
}

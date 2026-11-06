using System;
using System.IO;
using System.Runtime.InteropServices;
class Program
{
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    static extern int RegOpenKeyEx(IntPtr h, string path, int opts, int access, out IntPtr result);
    [DllImport("advapi32.dll")]
    static extern int RegOverridePredefKey(IntPtr h, IntPtr replacement);
    [DllImport("advapi32.dll")]
    static extern int RegCloseKey(IntPtr h);
    static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--runtime")
        {
            var t = typeof(object).Assembly.GetType("System.Globalization.GlobalizationMode");
            var flags = System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic |
                        System.Reflection.BindingFlags.Static;
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(
                new { runtime = Environment.Version.ToString(),
                      invariant = (bool)(t.GetProperty("Invariant", flags)?.GetValue(null) ??
                                         t.GetField("Invariant", flags)?.GetValue(null) ?? false),
                      nls = (bool)(t.GetProperty("UseNls", flags)?.GetValue(null) ??
                                   t.GetField("UseNls", flags)?.GetValue(null) ?? false),
                      corelib = typeof(object).Assembly.Location,
                      ui = System.Globalization.CultureInfo.CurrentUICulture.Name }));
            return;
        }
        IntPtr key = IntPtr.Zero;
        bool overridden = false;
        try
        {
            for (int i = 0; i < args.Length; i++)
                if (args[i] == "--registry")
                {
                    string path = args[++i];
                    const string prefix = @"Software\OpenSimNativeTimeZoneTests\";
                    if (!path.StartsWith(prefix, StringComparison.Ordinal) ||
                        !Guid.TryParse(path.Substring(prefix.Length), out _))
                        throw new Exception("Unowned fixture registry root");
                    int status =
                        RegOpenKeyEx(new IntPtr(unchecked((int)0x80000001)), path, 0, 0x20019, out key);
                    if (status != 0)
                        throw new Exception("Open fixture root: " + status);
                    status = RegOverridePredefKey(new IntPtr(unchecked((int)0x80000002)), key);
                    if (status != 0)
                        throw new Exception("Override fixture root: " + status);
                    overridden = true;
                }
            Run();
        }
        finally
        {
            if (overridden)
                RegOverridePredefKey(new IntPtr(unchecked((int)0x80000002)), IntPtr.Zero);
            if (key != IntPtr.Zero)
                RegCloseKey(key);
        }
    }
    static string H(string s)
    {
        if (s == null)
            return "-";
        var r = "";
        foreach (char c in s)
            r += ((int)c).ToString("x4");
        return r.Length == 0 ? "." : r;
    }
    static string E(Exception e)
    {
        if (e == null)
            return "-";
        return e.GetType().FullName + ";" + unchecked((uint)e.HResult).ToString("x8") + ";" + H(e.Message) +
               ";" + H((e as ArgumentException)?.ParamName) + ";" +
               H((e as FileNotFoundException)?.FileName) + ";[" + E(e.InnerException) + "]";
    }
    static void Run()
    {
        string l;
        while ((l = Console.ReadLine()) != null)
        {
            if (l == "!OOM_ALLOC")
            {
                try
                {
                    GC.KeepAlive(new byte[128 * 1024 * 1024]);
                    Console.WriteLine("NO_OOM");
                }
                catch (OutOfMemoryException ex)
                {
                    Console.WriteLine(E(ex));
                }
                continue;
            }
            if (l == "!OOM")
            {
                Console.WriteLine(E(new OutOfMemoryException()));
                continue;
            }
            if (l == "!")
            {
                TimeZoneInfo.ClearCachedData();
                Console.WriteLine("CLEAR");
                continue;
            }
            try
            {
                long ticks = 638554752000000000L;
                int split = l.IndexOf('#');
                if (split >= 0)
                {
                    ticks =
                        long.Parse(l.Substring(split + 1), System.Globalization.CultureInfo.InvariantCulture);
                    l = l.Substring(0, split);
                }
                string id = null;
                if (l != "-")
                {
                    if (l == ".")
                        id = "";
                    else
                    {
                        var c = new char[l.Length / 4];
                        for (int i = 0; i < c.Length; i++)
                            c[i] = (char)Convert.ToInt32(l.Substring(i * 4, 4), 16);
                        id = new string(c);
                    }
                }
                var z = TimeZoneInfo.FindSystemTimeZoneById(id);
                Console.WriteLine("OK;" + H(z.Id) + ";" +
                                  (z.IsDaylightSavingTime(new DateTime(ticks, DateTimeKind.Utc)) ? 1 : 0) +
                                  ";" + (object.ReferenceEquals(id, z.Id) ? 1 : 0));
            }
            catch (Exception e)
            {
                Console.WriteLine(E(e));
            }
        }
    }
}
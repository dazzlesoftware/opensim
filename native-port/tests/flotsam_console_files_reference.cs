using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
static class Program
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateFileW(string path, uint access, uint share, IntPtr security, uint creation,
                                     uint flags, IntPtr template);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);
    static string Hex(string value) =>
        value == null ? "~" : Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    static string Run(string line)
    {
        var parts = line.Split(' ', 2);
        string op = parts[0],
               path = parts[1] == "~" ? null : Encoding.UTF8.GetString(Convert.FromHexString(parts[1]));
        IntPtr blocker = new(-1);
        try
        {
            if (op == "enum-lock" || op == "time-lock")
            {
                blocker = CreateFileW(path, 0, 0, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero);
                if (blocker == new IntPtr(-1))
                    throw new Exception("Test lock failed");
            }
            if (op == "enum" || op == "enum-lock")
                return "OK|" + string.Join(";", Directory.GetFiles(path, "*.fac").Select(Hex));
            var date = File.GetLastWriteTime(path);
            ulong data = (ulong) typeof(DateTime)
                             .GetField("_dateData", BindingFlags.NonPublic | BindingFlags.Instance)
                             .GetValue(date);
            return "OK|" + date.Ticks + "|" + (int)date.Kind + "|" + data;
        }
        catch (Exception error)
        {
            return "EX|" + error.GetType().FullName + "|" + error.HResult.ToString("X8") + "|" +
                   Hex((error as ArgumentException)?.ParamName) + "|" + Hex(error.Message);
        }
        finally
        {
            if (blocker != new IntPtr(-1))
                CloseHandle(blocker);
        }
    }
    public static void Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        if (args.Length > 0 && args[0] == "--zone")
        {
            Console.WriteLine(TimeZoneInfo.Local.Id);
            return;
        }
        string line;
        while ((line = Console.ReadLine()) != null)
            Console.WriteLine(Run(line));
    }
}

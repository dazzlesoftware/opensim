using System;
using System.Collections.Generic;
using System.Text;
namespace Shim
{
    static class Path
    {
        public static char VolumeSeparatorChar, DirectorySeparatorChar, AltDirectorySeparatorChar;
        public static Func<string, string, string> CombineProvider;
        public static string Combine(string a, string b) => CombineProvider(a, b);
    }
    static class Directory
    {
        public static Func<string, string, string[]> Directories, Files;
        public static string[] GetDirectories(string p, string e) => Directories(p, e);
        public static string[] GetFiles(string p, string e) => Files(p, e);
    }
}
class Program
{
    static string Dec(string s)
    {
        if (s == "-")
            return null;
        if (s == ".")
            return "";
        var value = new char[s.Length / 4];
        for (int i = 0; i < value.Length; i++)
            value[i] = (char)(Convert.ToInt32(s.Substring(i * 4, 2), 16) |
                              (Convert.ToInt32(s.Substring(i * 4 + 2, 2), 16) << 8));
        return new string(value);
    }
    static string Hex(string s)
    {
        if (s.Length == 0)
            return ".";
        var o = new StringBuilder();
        foreach (char c in s)
            o.Append(((byte)c).ToString("x2")).Append(((byte)(c >> 8)).ToString("x2"));
        return o.ToString();
    }
    static string Error(Exception e) => "EX:" + e.GetType().FullName + ":" + unchecked((uint)e.HResult) +
                                        ":" + Hex(e.Message) + ":" +
                                        ((e as ArgumentException)?.ParamName is string p ? Hex(p) : "-");
    static string ArrayText(string[] values) =>
        "A" + (values.Length == 0 ? "" : ":" + string.Join(":", Array.ConvertAll(values, Hex)));
    class Probe
    {
        public int point, nth, hit, scenario, directories;
        public List<string> trace = new();
        void Event(int x, string t)
        {
            trace.Add(t);
            if (point == x && ++hit == nth)
                throw new Exception("fault");
        }
        public void Bind()
        {
            Shim.Directory.Directories = (p, e) =>
            {
                Event(1, "D:" + Hex(p) + ":" + Hex(e));
                directories++;
                if (scenario == 0 || directories > 12)
                    return Array.Empty<string>();
                if (scenario == 1)
                    return new[] { "dir-" + directories };
                if (scenario == 2)
                    return new[] { "dup", "dup" };
                return new[] { "/absolute", "\\rooted", "C:relative", "",
                               "raw-" + new string((char)0xd800, 1) };
            };
            Shim.Directory.Files = (p, e) =>
            {
                Event(2, "F:" + Hex(p) + ":" + Hex(e));
                return new[] { p + "|" + e, "duplicate", "duplicate", "", new string((char)0xd800, 1) };
            };
            Shim.Path.CombineProvider = (a, b) =>
            {
                Event(3, "C:" + Hex(a) + ":" + Hex(b));
                return scenario == 1 ? "[" + a + "]{" + b + "}" : System.IO.Path.Combine(a, b);
            };
        }
    }
    static string Run(string row)
    {
        var a = row.Split(' ');
        string mode = a[0];
        var p = new Probe { point = int.Parse(a[4]), nth = int.Parse(a[5]), scenario = int.Parse(a[6]) };
        Shim.Path.VolumeSeparatorChar = (char)int.Parse(a[1]);
        Shim.Path.DirectorySeparatorChar = (char)int.Parse(a[2]);
        Shim.Path.AltDirectorySeparatorChar = (char)int.Parse(a[3]);
        p.Bind();
        string result;
        try
        {
            if (mode == "B")
                result = "B:" + Hex(System.IO.Path.Combine(Dec(a[7]), Dec(a[8])));
            else
            {
                if (mode == "F")
                {
                    Shim.Path.VolumeSeparatorChar = System.IO.Path.VolumeSeparatorChar;
                    Shim.Path.DirectorySeparatorChar = System.IO.Path.DirectorySeparatorChar;
                    Shim.Path.AltDirectorySeparatorChar = System.IO.Path.AltDirectorySeparatorChar;
                    Shim.Path.CombineProvider = System.IO.Path.Combine;
                    Shim.Directory.Directories = System.IO.Directory.GetDirectories;
                    Shim.Directory.Files = System.IO.Directory.GetFiles;
                }
                result = ArrayText(SourceUtil.Glob(Dec(a[7])));
            }
        }
        catch (Exception e)
        {
            result = Error(e);
        }
        if (mode == "G")
            result += "/IO:" + string.Join(";", p.trace);
        return result;
    }
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--runtime")
        {
            Console.WriteLine(Environment.Version);
            Console.WriteLine(typeof(System.IO.Path).Assembly.Location);
            return;
        }
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
            Console.WriteLine(Run(row));
    }
}

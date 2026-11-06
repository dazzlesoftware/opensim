using System;
using System.IO;
using System.IO.Enumeration;
using System.Reflection;
using System.Text;
using System.Collections.Generic;
class Program
{
    static string Dec(string s)
    {
        if (s == "-")
            return null;
        if (s == ".")
            return "";
        var t = new char[s.Length / 4];
        for (int i = 0; i < t.Length; i++)
            t[i] = (char)(Convert.ToInt32(s.Substring(i * 4, 2), 16) |
                          (Convert.ToInt32(s.Substring(i * 4 + 2, 2), 16) << 8));
        return new string(t);
    }
    static string Hex(string s)
    {
        if (s.Length == 0)
            return ".";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((byte)c).ToString("x2")).Append(((byte)(c >> 8)).ToString("x2"));
        return b.ToString();
    }
    static string Error(Exception e) => e.GetType().FullName + ":" + unchecked((uint)e.HResult) + ":" +
                                        Hex(e.Message) + ":" +
                                        ((e as ArgumentException)?.ParamName is string p ? Hex(p) : "-");
    static bool Normalize(ref string directory, ref string expression)
    {
        ArgumentNullException.ThrowIfNull(directory, "path");
        ArgumentNullException.ThrowIfNull(expression, "searchPattern");
        var method = typeof(Directory)
                         .Assembly.GetType("System.IO.Enumeration.FileSystemEnumerableFactory")
                         .GetMethod("NormalizeInputs", BindingFlags.NonPublic | BindingFlags.Static);
        var args = new object[] { directory, expression, MatchType.Win32 };
        try
        {
            bool result = (bool)method.Invoke(null, args);
            directory = (string)args[0];
            expression = (string)args[1];
            return result;
        }
        catch (TargetInvocationException e)
        {
            throw e.InnerException;
        }
    }
    class P
    {
        public int point, nth, hit;
        public List<string> trace = new();
        public void Event(int x, string s)
        {
            trace.Add(s);
            if (point == x && ++hit == nth)
                throw new Exception("fault");
        }
        public string Collect(string path, string expression, bool dirs)
        {
            Normalize(ref path, ref expression);
            Event(1, "F:" + Hex(path));
            string full = OperatingSystem.IsWindows() ? "X:\\normalized\\" : "/normalized/";
            full = Path.TrimEndingDirectorySeparator(full);
            Event(2, "O:" + Hex(full));
            var entries =
                new(string name, bool dir)[] { (".", true),          ("..", true),
                                               ("alpha.txt", false), ("ALPHA.TXT", false),
                                               ("alpha", false),     ("sub", true),
                                               ("SUB.EXT", true),    ("配置😀", false),
                                               ("<literal>", false), (new string((char)0xd800, 1), false),
                                               (".hidden", false),   ("a.b.c", false) };
            var values = new List<string>();
            try
            {
                int index = 0;
                while (true)
                {
                    Event(3, "N:" + index);
                    if (index >= entries.Length)
                        break;
                    var entry = entries[index++];
                    if (entry.dir && (entry.name == "." || entry.name == ".."))
                        continue;
                    if (entry.dir != dirs)
                        continue;
                    bool ignore = OperatingSystem.IsWindows() || OperatingSystem.IsMacOS();
                    if (FileSystemName.MatchesWin32Expression(expression, entry.name, ignore))
                        values.Add(Path.Join(path, entry.name));
                }
            }
            finally
            {
                Event(4, "C");
            }
            return "A" + (values.Count == 0 ? "" : ":" + string.Join(":", values.ConvertAll(Hex)));
        }
    }
    static string Run(string row)
    {
        var args = row.Split(' ');
        string mode = args[0];
        int target = int.Parse(args[1]);
        var path = Dec(args[2]);
        var expr = Dec(args[3]);
        var p = new P { point = int.Parse(args[4]), nth = int.Parse(args[5]) };
        string result;
        try
        {
            if (mode == "N")
            {
                string originalPath = path, originalExpr = expr;
                bool unchanged = Normalize(ref path, ref expr);
                result = Hex(path) + ":" + Hex(expr) + ":" +
                         (Object.ReferenceEquals(path, originalPath) ? "1" : "0") + ":" +
                         (Object.ReferenceEquals(expr, originalExpr) ? "1" : "0") + ":" +
                         (Object.ReferenceEquals(expr, "*") ? "1" : "0") + ":" + (unchanged ? "1" : "0");
            }
            else if (mode == "P")
                result = p.Collect(path, expr, target != 0);
            else
            {
                string[] values = target == 2   ? Directory.GetFiles(path)
                                  : target == 3 ? Directory.GetDirectories(path)
                                  : target != 0 ? Directory.GetDirectories(path, expr)
                                                : Directory.GetFiles(path, expr);
                result =
                    "A" + (values.Length == 0 ? "" : ":" + string.Join(":", Array.ConvertAll(values, Hex)));
            }
        }
        catch (Exception e)
        {
            result = "EX:" + Error(e);
        }
        if (mode == "P")
            result += "/IO:" + string.Join(";", p.trace);
        return result;
    }
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--runtime")
        {
            Console.WriteLine(Environment.Version);
            Console.WriteLine(typeof(Directory).Assembly.Location);
            return;
        }
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
            Console.WriteLine(Run(row));
    }
}

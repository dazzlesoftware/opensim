using System;
using System.Text;
using System.Globalization;
using Nini.Config;
using Nini.Util;
class Program
{
    static string H(string s)
    {
        if (s == null)
            return "~";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.Length == 0 ? "-" : b.ToString();
    }
    static string D(string s)
    {
        if (s == "~")
            return null;
        if (s == "-")
            return "";
        var b = new StringBuilder();
        for (int i = 0; i < s.Length; i += 4)
            b.Append((char)Convert.ToInt32(s.Substring(i, 4), 16));
        return b.ToString();
    }
    static string E(Exception e) => e.GetType().FullName + ":" + H(e.Message) + ":" +
                                    (e is ArgumentException a ? H(a.ParamName) : "~") + ":" +
                                    unchecked((uint)e.HResult).ToString("x8");
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            var output = new System.Collections.Generic.List<string>();
            ArgvParser parser = null;
            ArgvConfigSource source = null;
            string[] args = null;
            foreach (var operation in row.Split('|'))
            {
                var a = operation.Split(' ');
                try
                {
                    switch (a[0])
                    {
                    case "U":
                        var uc = D(a[1]);
                        output.Add((System.Text.RegularExpressions.Regex.IsMatch(uc, @"^\w$") ? "1" : "0") +
                                   (System.Text.RegularExpressions.Regex.IsMatch(uc, @"^\s$") ? "1" : "0"));
                        break;
                    case "P":
                    case "S":
                        args = a[1] == "~"   ? null
                               : a[1] == "!" ? Array.Empty<string>()
                                             : Array.ConvertAll(a[1..], D);
                        if (a[0] == "P")
                            parser = new ArgvParser(args);
                        else
                            source = new ArgvConfigSource(args);
                        break;
                    case "C":
                        parser = new ArgvParser(D(a[1]));
                        break;
                    case "G":
                        output.Add(H(parser[D(a[1])]));
                        break;
                    case "A":
                        source.AddSwitch(D(a[1]), D(a[2]), D(a[3]));
                        break;
                    case "M":
                        args[int.Parse(a[1])] = D(a[2]);
                        break;
                    case "V":
                        source.Save();
                        break;
                    case "R":
                        source.Reload();
                        break;
                    case "AS":
                        source.AutoSave = a[1] == "1";
                        break;
                    case "APP":
                        ApplicationArgvSetup.Apply(source);
                        break;
                    case "EV":
                        int mode = int.Parse(a[1]);
                        source.Configs.ConfigAdded += (sender, eventArgs) =>
                        {
                            output.Add("added");
                            if (mode == 1)
                                throw new InvalidOperationException("event-fault");
                            if (mode == 2)
                            {
                                source.Configs.Clear();
                            }
                            if (mode == 3)
                            {
                                source.AddSwitch("nested", "x");
                            }
                        };
                        break;
                    case "ID":
                        output.Add(Object.ReferenceEquals(source.Configs[D(a[1])].Get(D(a[2])),
                                                          args[int.Parse(a[3])])
                                       ? "same"
                                       : "different");
                        break;
                    case "ARGS":
                        output.Add(string.Join(",", Array.ConvertAll(source.GetArguments(), H)));
                        break;
                    case "SN":
                        var b = new StringBuilder();
                        foreach (IConfig c in source.Configs)
                        {
                            b.Append(H(c.Name) + "[");
                            foreach (var k in c.GetKeys())
                                b.Append(H(k) + "=" + H(c.Get(k)) + ",");
                            b.Append("]");
                        }
                        output.Add(b.ToString());
                        break;
                    }
                    output.Add("ok");
                }
                catch (Exception e)
                {
                    output.Add(E(e));
                }
            }
            Console.WriteLine(string.Join("/", output));
        }
    }
}

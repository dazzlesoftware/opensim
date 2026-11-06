using System;
using System.Text;
using System.Globalization;
using Nini.Config;
static class Program
{
    static string Hex(string value) =>
        value == null       ? "-"
        : value.Length == 0 ? "."
                            : Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    static string Run(string line)
    {
        var p = line.Split(' ');
        var source = new IniConfigSource();
        if (p[1] == "1")
        {
            source.Alias.AddAlias("On", true);
            source.Alias.AddAlias("Off", false);
            source.Alias.AddAlias("Yes", true);
            source.Alias.AddAlias("No", false);
        }
        var section = source.AddConfig("Probe");
        if (p[2] == "1")
            section.Set("x", p[3] == "." ? "" : Encoding.UTF8.GetString(Convert.FromHexString(p[3])));
        try
        {
            return p[0] switch { "s" => "S:" + Hex(section.GetString("x", "fallback")),
                                 "n" => "S:" + Hex(section.GetString("x", null)),
                                 "b" => "B:" + (section.GetBoolean("x", true) ? 1 : 0),
                                 "i" => "I:" + section.GetInt("x", -99),
                                 "l" => "L:" + section.GetLong("x", long.MinValue),
                                 "r" => "R:" + BitConverter.DoubleToUInt64Bits(section.GetDouble("x", -0.0)),
                                 _ => throw new Exception("operation") };
        }
        catch (Exception e)
        {
            return "EX:" + e.GetType().Name + ":" + unchecked((uint)e.HResult) + ":" +
                   Hex((e as ArgumentException)?.ParamName) + ":" + Hex(e.Message);
        }
    }
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        Console.OutputEncoding = new UTF8Encoding(false);
        for (string line; (line = Console.ReadLine()) != null;)
            Console.WriteLine(Run(line));
    }
}

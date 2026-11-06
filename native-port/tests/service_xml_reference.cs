using System;
using System.Text;
using System.Xml;
using System.IO;
using System.Collections.Generic;
public static class Util
{
    public static Encoding UTF8NoBomEncoding = new UTF8Encoding(false);
}
public class Logger
{
    public void Debug(object value)
    {
    }
}
public static partial class Reference
{
    static Logger m_log = new();
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Snap(object value)
    {
        if (value is not Dictionary<string, object> d)
            return "S" + H((string)value);
        string output = "D" + d.Count + "{";
        foreach (var p in d)
            output += H(p.Key) + "=" + Snap(p.Value) + ";";
        return output + "}";
    }
    public static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
            Console.WriteLine(Snap(ParseXmlResponse(Encoding.UTF8.GetString(Convert.FromHexString(line)))));
    }
}

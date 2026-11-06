using System;
using System.Xml;
using System.Text;
class Program
{
    static string Hex(string v)
    {
        var b = new StringBuilder();
        foreach (char c in v)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static string? Decode(string v)
    {
        if (v == "~")
            return null;
        if (v == "-")
            return "";
        var b = new StringBuilder();
        for (int i = 0; i < v.Length; i += 4)
            b.Append((char)Convert.ToInt32(v.Substring(i, 4), 16));
        return b.ToString();
    }
    static void Error(Exception e)
    {
        Console.Write("E:" + e.GetType().FullName + ":" + e.HResult + ":" + Hex(e.Message) + ":" +
                      (e is ArgumentException a && a.ParamName != null ? Hex(a.ParamName) : "~"));
    }
    static void Main()
    {
        string? row;
        var resolver = new XmlUrlResolver();
        while ((row = Console.ReadLine()) != null)
        {
            var pieces = row.Split(' ');
            try
            {
                var text = Decode(pieces[0]);
                var b = text == null ? null : new Uri(text, UriKind.RelativeOrAbsolute);
                var uri = resolver.ResolveUri(b, Decode(pieces[1]));
                Console.Write((ReferenceEquals(b, uri) ? "S" : "N") + ":" + (uri.IsAbsoluteUri ? "A" : "R"));
                for (int i = 0; i < 5; i++)
                {
                    Console.Write("|");
                    try
                    {
                        Console.Write(Hex(i switch {
                            0 => uri.OriginalString,
                            1 => uri.ToString(),
                            2 => uri.AbsoluteUri,
                            3 => uri.LocalPath,
                            _ => uri.Scheme
                        }));
                    }
                    catch (Exception e)
                    {
                        Error(e);
                    }
                }
            }
            catch (Exception e)
            {
                Error(e);
            }
            Console.WriteLine();
        }
    }
}

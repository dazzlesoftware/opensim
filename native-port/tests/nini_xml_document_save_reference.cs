using System;
using System.IO;
using System.Text;
using System.Xml;
using System.Reflection;
class Program
{
    static string Hex(string? s)
    {
        if (s == null)
            return "~";
        if (s.Length == 0)
            return "-";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static string? Decode(string s)
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
    static string Error(Exception e)
    {
        while (e is TargetInvocationException && e.InnerException != null)
            e = e.InnerException;
        return e.GetType().FullName + ":" + e.HResult.ToString("x8") + ":" + Hex(e.Message) + ":" +
               Hex((e as ArgumentException)?.ParamName);
    }
    static void Main()
    {
        string? line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int mode = int.Parse(p[0]), preserve = int.Parse(p[1]), closed = int.Parse(p[2]);
            var sink = new StringWriter();
            sink.NewLine = Decode(p[3])!;
            if (closed == 1)
                sink.Close();
            var doc = new XmlDocument { PreserveWhitespace = preserve != 0 };
            string failure = "-";
            if (mode >= 6)
            {
                string? output = null;
                try
                {
                    using var reader =
                        XmlReader.Create(new StringReader(Decode(p[4])!),
                                         new XmlReaderSettings { DtdProcessing = DtdProcessing.Ignore });
                    var source = new Nini.Config.XmlConfigSource(reader);
                    if (mode == 7)
                    {
                        var config = source.Configs["Added"] ?? source.AddConfig("Added");
                        config.Set("Escaped", "<&>\"'\r\n");
                    }
                    if (mode == 8)
                        source.Configs.Clear();
                    if (mode == 9)
                    {
                        var other = new Nini.Config.XmlConfigSource();
                        other.AddConfig("Merge").Set("from", "other");
                        source.Merge(other);
                    }
                    output = source.ToString();
                }
                catch (Exception e)
                {
                    failure = Error(e);
                }
                Console.WriteLine(Hex(output) + ";" + failure + ";open");
                continue;
            }
            try
            {
                if (p[4] == "decl1" || p[4] == "declbad")
                    doc.AppendChild(doc.CreateXmlDeclaration(
                        "1.1", p[4] == "decl1" ? "utf-8" : "not-an-encoding", "yes"));
                else if (p[4] != "empty")
                    doc.LoadXml(Decode(p[4])!);
                TextWriter target = closed < 0 ? null! : sink;
                if (mode == 0)
                    doc.Save(target);
                else
                {
                    var writer = (XmlTextWriter)Activator.CreateInstance(
                        typeof(XmlTextWriter).Assembly.GetType("System.Xml.XmlDOMTextWriter")!,
                        BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance, null,
                        new object[] { target }, null)!;
                    if (preserve == 0)
                        writer.Formatting = Formatting.Indented;
                    if (mode == 2)
                        writer.WriteComment("before");
                    if (mode == 3)
                        writer.WriteStartDocument();
                    if (mode == 4)
                        writer.Close();
                    if (mode == 1)
                    {
                        doc.WriteTo(writer);
                        writer.Flush();
                    }
                    else
                        doc.Save(writer);
                }
            }
            catch (Exception e)
            {
                failure = Error(e);
            }
            string before = sink.ToString(), after = "open";
            try
            {
                sink.Write("!");
            }
            catch (Exception e)
            {
                after = Error(e);
            }
            Console.WriteLine(Hex(before) + ";" + failure + ";" + after);
        }
    }
}

using System;
using System.IO;
using System.Text;
using System.Xml;
using System.Reflection;
class Sink : TextWriter
{
    readonly StringBuilder b = new();
    public Action Hook = () =>
    {};
    public override Encoding Encoding => Encoding.Unicode;
    public override string ToString() => b.ToString();
    public override void Write(char c)
    {
        Hook();
        b.Append(c);
    }
    public override void Write(string? s)
    {
        Hook();
        b.Append(s);
    }
    public override void Write(char[] v, int i, int n)
    {
        Hook();
        b.Append(v, i, n);
    }
    public override void Write(char[]? v)
    {
        Hook();
        b.Append(v);
    }
    public override void Write(ReadOnlySpan<char> s)
    {
        Hook();
        b.Append(s);
    }
    public override void WriteLine()
    {
        Hook();
        b.Append(NewLine);
    }
    public override void Flush()
    {
        Hook();
    }
}
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
    static string Decode(string s)
    {
        var b = new StringBuilder();
        if (s != "-")
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
            int action = int.Parse(p[0]), nth = int.Parse(p[1]), mode = int.Parse(p[2]);
            var doc = new XmlDocument();
            doc.LoadXml(Decode(p[3]));
            var root = doc.DocumentElement!;
            var sink = new Sink();
            int seen = 0;
            bool fired = false;
            sink.Hook = () =>
            {
                if (++seen != nth)
                    return;
                fired = true;
                switch (action)
                {
                case 9:
                    root.Attributes["a"]?.AppendChild(doc.CreateTextNode("appended"));
                    break;
                case 10:
                    if (root.Attributes["a"]?.FirstChild is XmlNode first)
                        root.Attributes["a"]!.RemoveChild(first);
                    break;
                case 11:
                    root.Attributes["a"]?.AppendChild(doc.CreateDocumentFragment());
                    break;
                case 12:
                    if (root.Attributes["a"]?.FirstChild is XmlNode childText)
                        childText.Value = "child";
                    break;
                case 8:
                    throw new IOException("writer fault");
                case 0:
                    if (root.FirstChild != null)
                        root.RemoveChild(root.FirstChild);
                    break;
                case 1:
                    root.AppendChild(doc.CreateElement("added"));
                    break;
                case 2:
                    doc.RemoveAll();
                    break;
                case 3:
                    root.IsEmpty = true;
                    break;
                case 4:
                    var a = doc.CreateAttribute("extra");
                    a.Value = "new<&";
                    root.Attributes.Append(a);
                    break;
                case 5:
                    if (root.Attributes["a"] != null)
                        root.Attributes["a"]!.Value = "changed";
                    break;
                case 6:
                    if (root.LastChild != null)
                        root.LastChild.Value = "changed";
                    break;
                case 7:
                    if (root.FirstChild is XmlElement child)
                        child.AppendChild(doc.CreateTextNode("new"));
                    break;
                }
            };
            var writer = (XmlTextWriter)Activator.CreateInstance(
                typeof(XmlTextWriter).Assembly.GetType("System.Xml.XmlDOMTextWriter")!,
                BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance, null,
                new object[] { sink }, null)!;
            writer.Formatting = Formatting.Indented;
            string failure = "-";
            try
            {
                if (mode == 0)
                    doc.Save(writer);
                else
                {
                    doc.WriteTo(writer);
                    writer.Flush();
                }
            }
            catch (Exception e)
            {
                failure = Error(e);
            }
            Console.WriteLine(Hex(sink.ToString()) + ";" + failure + ";" + (int)writer.WriteState + ";" +
                              seen + ";" + (fired ? 1 : 0));
        }
    }
}

using System;
using System.IO;
using System.Text;
using System.Xml;
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
    static string Error(Exception e) => e.GetType().FullName + ":" + e.HResult.ToString("x8") + ":" +
                                        Hex(e.Message) + ":" + Hex((e as ArgumentException)?.ParamName);
    static string Bytes(byte[] b) => b.Length == 0 ? "-" : Convert.ToHexString(b).ToLowerInvariant();
    static void Main(string[] args)
    {
        string path = Path.Combine(Path.GetFullPath(args[0]), "output.xml");
        string? line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int mode = int.Parse(p[0]), evt = int.Parse(p[1]), saved = 0;
            Nini.Config.XmlConfigSource? source = null;
            var memory = new MemoryStream();
            var text = new StringWriter();
            string failure = "-", payload = "~";
            File.WriteAllText(path, "previous", new UTF8Encoding(false));
            try
            {
                using var reader =
                    XmlReader.Create(new StringReader(Decode(p[2])),
                                     new XmlReaderSettings { DtdProcessing = DtdProcessing.Parse });
                source = new Nini.Config.XmlConfigSource(reader);
                source.Saved += (sender, e) =>
                {
                    ++saved;
                    if (evt == 1)
                        throw new InvalidOperationException("saved event fault");
                    if (evt == 2)
                        source.AddConfig("Event").Set("key", "value");
                };
                source.AddConfig("Added").Set("Escaped", "<&>\"'\r\n");
                switch (mode)
                {
                case 0:
                    payload = Hex(source.ToString());
                    break;
                case 1:
                    source.Save(text);
                    break;
                case 2:
                    source.Save(memory);
                    break;
                case 3:
                    source.Save(new StreamWriter(memory));
                    break;
                case 4:
                    source.Save(path);
                    break;
                case 5:
                    source.Save((TextWriter)null!);
                    break;
                case 6:
                    source.Save((Stream)null!);
                    break;
                case 7:
                    source.Save(Path.GetFullPath(args[0]));
                    break;
                }
            }
            catch (Exception e)
            {
                failure = Error(e);
            }
            if (mode == 1)
                payload = Hex(text.ToString());
            if (mode == 2 || mode == 3)
                payload = Bytes(memory.ToArray());
            if (mode == 4 || mode == 7)
                payload = Bytes(File.ReadAllBytes(path));
            string? post = null;
            string posterror = "-";
            try
            {
                post = source?.ToString();
            }
            catch (Exception e)
            {
                posterror = Error(e);
            }
            Console.WriteLine(failure + ";" + payload + ";" + saved + ";" + Hex(source?.SavePath) + ";" +
                              (!memory.CanWrite ? 1 : 0) + ";" + Hex(post) + ";" + posterror);
        }
    }
}

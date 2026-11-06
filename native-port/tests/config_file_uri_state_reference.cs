using System;
using System.Reflection;
using System.Linq;
class Program
{
    static string Encode(string s) => string.Concat(s.Select(c => ((int)c).ToString("x4")));
    static void Main()
    {
        var minimal =
            typeof(Uri).GetMethod("PrivateParseMinimal", BindingFlags.Instance | BindingFlags.NonPublic);
        var remaining =
            typeof(Uri).GetMethod("EnsureParseRemaining", BindingFlags.Instance | BindingFlags.NonPublic);
        var flagsField = typeof(Uri).GetField("_flags", BindingFlags.Instance | BindingFlags.NonPublic);
        var stringField = typeof(Uri).GetField("_string", BindingFlags.Instance | BindingFlags.NonPublic);
        var originalField =
            typeof(Uri).GetField("_originalUnicodeString", BindingFlags.Instance | BindingFlags.NonPublic);
        var syntaxField = typeof(Uri).GetField("_syntax", BindingFlags.Instance | BindingFlags.NonPublic);
        var infoField = typeof(Uri).GetField("_info", BindingFlags.Instance | BindingFlags.NonPublic);
        var flagsType = typeof(Uri).GetNestedType("Flags", BindingFlags.NonPublic);
        var file = typeof(UriParser)
                       .GetField("FileUri", BindingFlags.NonPublic | BindingFlags.Static)
                       .GetValue(null);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            ulong flags = ulong.Parse(p[0]);
            string text = "";
            for (int i = 0; i < p[1].Length; i += 4)
                text += (char)Convert.ToUInt16(p[1].Substring(i, 4), 16);
            var uri = new Uri("file:///x");
            flagsField.SetValue(uri, Enum.ToObject(flagsType, flags));
            stringField.SetValue(uri, text);
            originalField.SetValue(uri, text);
            syntaxField.SetValue(uri, file);
            int error = Convert.ToInt32(minimal.Invoke(uri, null));
            string status = error != 0 ? "P" + error : "S";
            if (error == 0)
                try
                {
                    remaining.Invoke(uri, null);
                }
                catch (TargetInvocationException e)
                {
                    status = "E" + e.InnerException.Message;
                }
            var info = infoField.GetValue(uri);
            string offsets = "N";
            if (info != null)
            {
                var offset = info.GetType()
                                 .GetField("Offset", BindingFlags.Instance | BindingFlags.Public |
                                                         BindingFlags.NonPublic)
                                 .GetValue(info);
                offsets = string.Join(
                    ",", new[] { "Scheme", "User", "Host", "PortValue", "Path", "Query", "Fragment", "End" }
                             .Select(n => offset.GetType()
                                              .GetField(n, BindingFlags.Instance | BindingFlags.Public |
                                                               BindingFlags.NonPublic)
                                              .GetValue(offset)));
            }
            Console.WriteLine(
                $"{status}:{Convert.ToUInt64(flagsField.GetValue(uri))}:{Encode((string)stringField.GetValue(uri))}:{offsets}");
        }
    }
}

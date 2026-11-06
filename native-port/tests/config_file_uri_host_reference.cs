using System;
using System.Reflection;
using System.Linq;
unsafe class Program
{
    static string Encode(string s) => s == null ? "-" : string.Concat(s.Select(c => ((int)c).ToString("x4")));
    static void Main()
    {
        var method =
            typeof(Uri).GetMethod("CheckAuthorityHelper", BindingFlags.Instance | BindingFlags.NonPublic);
        var flagsType = typeof(Uri).GetNestedType("Flags", BindingFlags.NonPublic);
        var errType = typeof(Uri).Assembly.GetType("System.ParsingError");
        var parser = typeof(UriParser)
                         .GetField("FileUri", BindingFlags.NonPublic | BindingFlags.Static)
                         .GetValue(null);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var parts = line.Split(' ');
            int start = int.Parse(parts[0]);
            ulong flags = ulong.Parse(parts[1]);
            string text = "";
            if (parts[2] != "-")
                for (int i = 0; i < parts[2].Length; i += 4)
                    text += (char)Convert.ToUInt16(parts[2].Substring(i, 4), 16);
            var uri = new Uri("file:///x");
            typeof(Uri)
                .GetField("_originalUnicodeString", BindingFlags.NonPublic | BindingFlags.Instance)
                .SetValue(uri, text);
            fixed(char *ptr = text)
            {
                object[] args = {
                    Pointer.Box(ptr, typeof(char *)), start,  text.Length, Enum.ToObject(errType, 0),
                    Enum.ToObject(flagsType, flags),  parser, null
                };
                int result = (int)method.Invoke(uri, args);
                Console.WriteLine(
                    $"{result}:{Convert.ToUInt64(args[4])}:{Convert.ToInt32(args[3])}:{Encode((string)args[6])}");
            }
        }
    }
}

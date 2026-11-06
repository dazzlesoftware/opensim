using System;
using System.Reflection;
using System.Linq;
class Program
{
    static string Encode(string s) => string.Concat(s.Select(c => ((int)c).ToString("x4")));
    static void Main()
    {
        var parts =
            typeof(Uri).GetMethod("GetComponentsHelper", BindingFlags.Instance | BindingFlags.NonPublic);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int format = int.Parse(p[0]);
            string text = "";
            for (int i = 0; i < p[1].Length; i += 4)
                text += (char)Convert.ToUInt16(p[1].Substring(i, 4), 16);
            try
            {
                var uri = new Uri(text, UriKind.Absolute);
                var result =
                    (string)parts.Invoke(uri, new object[] { UriComponents.Path | UriComponents.KeepDelimiter,
                                                             (UriFormat)format });
                Console.WriteLine("S" + Encode(result));
            }
            catch (Exception e)
            {
                if (e is TargetInvocationException t)
                    e = t.InnerException;
                Console.WriteLine("E" + Encode(e.Message));
            }
        }
    }
}

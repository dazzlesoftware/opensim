using System;
using System.Globalization;
using System.Linq;
class Program
{
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            string s = line == "null" ? null
                       : line == "-"
                           ? ""
                           : new string(Enumerable.Range(0, line.Length / 4)
                                            .Select(i => (char)Convert.ToUInt16(line.Substring(i * 4, 4), 16))
                                            .ToArray());
            Console.WriteLine(Uri.TryCreate(s, UriKind.Absolute, out var uri) &&
                                      (uri.Scheme == Uri.UriSchemeHttp || uri.Scheme == Uri.UriSchemeHttps)
                                  ? "1"
                                  : "0");
        }
    }
}

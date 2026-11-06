using System;
using System.Text;
using System.Globalization;
class PrefixReference
{
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split('|');
            var text = Encoding.UTF8.GetString(Convert.FromHexString(p[0]));
            var prefix = Encoding.UTF8.GetString(Convert.FromHexString(p[1]));
            Console.WriteLine(text.StartsWith(prefix) ? "1" : "0");
        }
    }
}

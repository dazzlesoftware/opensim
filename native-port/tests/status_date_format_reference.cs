using System;
using System.IO;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Text;
static class Program
{
    public static void Main()
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        using var output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false), 65536);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var parts = line.Split(' ');
            ulong ticks = ulong.Parse(parts[0]);
            int kind = int.Parse(parts[1]);
            if (ticks > (ulong)DateTime.MaxValue.Ticks || kind < 0 || kind > 3)
                throw new Exception("The formatter fixture accepts only valid DateTime models");
            var value = Unsafe.BitCast<ulong, DateTime>(ticks | ((ulong)kind << 62));
            output.WriteLine(value.ToString("MM/dd/yyyy hh:mm:ss"));
        }
    }
}

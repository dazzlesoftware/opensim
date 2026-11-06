using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
static class Program
{
    static string Decode(string hex)
    {
        if (hex == ".")
            return "";
        var chars = new char[hex.Length / 4];
        for (int i = 0; i < chars.Length; ++i)
            chars[i] = (char)(Convert.ToUInt16(hex.Substring(i * 4, 2), 16) |
                              (Convert.ToUInt16(hex.Substring(i * 4 + 2, 2), 16) << 8));
        return new string(chars);
    }
    static string Encode(string text)
    {
        var result = new System.Text.StringBuilder();
        foreach (char c in text)
        {
            result.Append(((byte)c).ToString("x2"));
            result.Append(((byte)(c >> 8)).ToString("x2"));
        }
        return result.ToString();
    }
    static void Main()
    {
        Console.OutputEncoding = new System.Text.UTF8Encoding(false);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                var values = Parser.Parse(line == "-" ? null : Decode(line));
                Console.Write(values.Length);
                foreach (var value in values)
                    Console.Write("|" + Encode(value));
                Console.WriteLine();
            }
            catch (Exception e)
            {
                Console.WriteLine("EX:" + e.Message);
            }
        }
    }
}

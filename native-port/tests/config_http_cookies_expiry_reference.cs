using System;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
static class Program
{
    static DateTime Raw(ulong bits) => Unsafe.BitCast<ulong, DateTime>(bits);
    static ulong Bits(DateTime value) => Unsafe.BitCast<DateTime, ulong>(value);
    static string Encode(string text)
    {
        var outp = new StringBuilder();
        foreach (char c in text)
        {
            outp.Append(((byte)c).ToString("x2"));
            outp.Append(((byte)(c >> 8)).ToString("x2"));
        }
        return outp.Length == 0 ? "." : outp.ToString();
    }
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
    static readonly MethodInfo utcOffset =
        Array.Find(typeof(TimeZoneInfo).GetMethods(BindingFlags.Static | BindingFlags.NonPublic),
                   m => m.Name == "GetUtcOffsetFromUtc" && m.GetParameters().Length == 4);
    static readonly MethodInfo tryCreate =
        typeof(DateTime).GetMethod("TryCreate", BindingFlags.Static | BindingFlags.NonPublic);
    static void Main()
    {
        CultureInfo.CurrentCulture = new CultureInfo("", false);
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        Console.OutputEncoding = new UTF8Encoding(false);
        for (string line; (line = Console.ReadLine()) != null;)
        {
            try
            {
                var parts = line.Split(' ');
                switch (parts[0])
                {
                case "meta":
                    var info = CultureInfo.CurrentCulture.DateTimeFormat;
                    Console.WriteLine("M|" + Environment.Version + "|" + info.Calendar.TwoDigitYearMax + "|" +
                                      TimeZoneInfo.Local.Id + "|" + typeof(DateTime).Assembly.Location);
                    var array =
                        (Array) typeof(DateTimeFormatInfo)
                            .GetMethod("CreateTokenHashTable", BindingFlags.Instance | BindingFlags.NonPublic)
                            .Invoke(info, null);
                    int i = 0;
                    foreach (var token in array)
                    {
                        if (token == null)
                        {
                            Console.WriteLine("T|" + i + "|~|0|0");
                        }
                        else
                        {
                            var type = token.GetType();
                            var flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
                            Console.WriteLine(
                                "T|" + i + "|" +
                                Encode((string)type.GetField("tokenString", flags).GetValue(token)) + "|" +
                                Convert.ToInt32(type.GetField("tokenType", flags).GetValue(token)) + "|" +
                                (int)type.GetField("tokenValue", flags).GetValue(token));
                        }
                        ++i;
                    }
                    Console.WriteLine("END");
                    break;
                case "F":
                    Console.WriteLine(
                        Encode(new DateTime(long.Parse(parts[1]), (DateTimeKind) int.Parse(parts[2]))
                                   .ToString(parts[3], CultureInfo.CurrentCulture)));
                    break;
                case "P":
                    var now = DateTime.Now;
                    bool parsed =
                        DateTime.TryParse(parts[1] == "~" ? null : Decode(parts[1]),
                                          CultureInfo.InvariantCulture, (DateTimeStyles)23, out var result);
                    Console.WriteLine("R:" + (parsed ? 1 : 0) + "|" + result.Ticks + "|" + (int)result.Kind +
                                      "|" + Bits(result) + "|" + Bits(now).ToString("x16"));
                    break;
                case "OFFSET":
                    Console.WriteLine(
                        TimeZoneInfo.Local.GetUtcOffset(Raw(ulong.Parse(parts[1], NumberStyles.HexNumber)))
                            .Ticks);
                    break;
                case "UTC":
                    var arguments = new object[] { new DateTime(long.Parse(parts[1]), DateTimeKind.Utc),
                                                   TimeZoneInfo.Local, false, false };
                    var offset = (TimeSpan)utcOffset.Invoke(null, arguments);
                    Console.WriteLine(offset.Ticks + "|" + ((bool)arguments[3] ? 1 : 0));
                    break;
                case "LEAP":
                    var inputs = new object[] { int.Parse(parts[1]),
                                                int.Parse(parts[2]),
                                                int.Parse(parts[3]),
                                                int.Parse(parts[4]),
                                                int.Parse(parts[5]),
                                                60,
                                                0,
                                                DateTime.MinValue };
                    Console.WriteLine((bool)tryCreate.Invoke(null, inputs) ? "1" : "0");
                    break;
                default:
                    throw new Exception("Unknown BCL query");
                }
            }
            catch (Exception error)
            {
                if (error is TargetInvocationException invocation && invocation.InnerException != null)
                    error = invocation.InnerException;
                Console.WriteLine("EX:" + error.GetType().Name + ":" + error.Message);
            }
        }
    }
}

using System;
using System.Text;
using System.IO;
using System.Linq;
using OpenMetaverse.StructuredData;
using System.Globalization;
class Program
{
    static string H(string value) => string.Concat(value.Select(c => ((int)c).ToString("x4")));
    static string Dump(OSD value)
    {
        if (value == null)
            return "N";
        if (value is OSDMap map)
            return "M" + map.Count + "[" +
                   string.Concat(map.dicvalue.Select(p => H(p.Key) + "=" + Dump(p.Value) + ";")) + "]";
        if (value is OSDArray array)
            return "A" + array.Count + "[" + string.Concat(array.Select(p => Dump(p) + ";")) + "]";
        if (value is OSDDate date)
            return "D" + date.value.Ticks + ":" + (int)date.value.Kind + ":" + H(value.AsString()) + ":" +
                   Convert.ToHexString(value.AsBinary()).ToLowerInvariant();
        if (value.Type == OSDType.Long)
            return "L" + value.AsLong();
        if (value is OSDBoolean)
            return value.AsBoolean() ? "B1" : "B0";
        if (value is OSDInteger)
            return "I" + value.AsInteger();
        if (value is OSDReal)
            return "R" + BitConverter.DoubleToUInt64Bits(value.AsReal()).ToString("x16");
        if (value is OSDUUID)
            return "Q" + value.AsUUID();
        if (value is OSDString)
            return "S" + H(value.AsString());
        if (value is OSDUri)
            return "V" + H(value.AsString());
        if (value is OSDBinary)
            return "X" + Convert.ToHexString(value.AsBinary()).ToLowerInvariant();
        return "U";
    }
    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            try
            {
                var stream = new MemoryStream(Convert.FromHexString(row));
                var mode = args.Length > 0 ? args[0] : "";
                var value = mode == "--json" ? OSDParser.DeserializeJson(stream)
                            : mode == "--text"
                                ? OSDParser.Deserialize(Encoding.UTF8.GetString(Convert.FromHexString(row)))
                            : mode == "--bytes"    ? OSDParser.Deserialize(Convert.FromHexString(row))
                            : mode == "--stream"   ? OSDParser.Deserialize(stream)
                            : mode == "--notation" ? OSDParser.DeserializeLLSDNotation(stream)
                            : mode == "--binary"   ? OSDParser.DeserializeLLSDBinary(stream)
                                                   : OSDParser.DeserializeLLSDXml(stream);
                var output = Dump(value);
                if (mode == "--json" || mode == "--stream" || mode == "--text" || mode == "--bytes")
                    output += "|" + H(OSDParser.SerializeJsonString(value, true));
                Console.WriteLine(output);
            }
            catch (Exception e)
            {
                Console.WriteLine("EX:" + H(e.Message));
            }
        }
    }
}

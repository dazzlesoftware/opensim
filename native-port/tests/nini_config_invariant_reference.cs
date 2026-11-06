// Copyright (c) Contributors, http://opensimulator.org/
// Actual bundled Nini.dll integration, with .NET8 invariant ICU providers.
using System;
using System.IO;
using System.Text;
using System.Globalization;
using Nini.Config;

class Program
{
    static void Require(bool value, string message)
    {
        if (!value)
            throw new Exception(message);
    }

    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        var content = "[Settings]\r\nanswer = 42\r\nround = 1.0000000596046447753906250000001\r\nzero = -0\r\nenabled = yEs\r\nKELVIN\u212a = value\r\nexpanded = ${answer}\r\n";
        var encoded = Encoding.Unicode.GetBytes(content);
        var preamble = Encoding.Unicode.GetPreamble();
        using var stream = new MemoryStream();
        stream.Write(preamble);
        stream.Write(encoded);
        stream.Position = 0;
        var source = new IniConfigSource(stream);
        var settings = source.Configs["Settings"];
        Require(settings != null, "UTF16 BOM document section");
        Require(settings.GetInt("answer") == 42, "INI through actual number provider");
        Require(BitConverter.SingleToUInt32Bits(settings.GetFloat("round")) == 0x3f800001, "INI direct Single rounding");
        Require(BitConverter.DoubleToUInt64Bits(settings.GetDouble("zero")) == 0x8000000000000000ul, "INI negative zero");
        source.Alias.AddAlias("Yes", true);
        Require(settings.GetBoolean("enabled"), "Captured invariant alias comparer");
        source.CaseSensitive = false;
        Require(settings.Get("kelvink") == "value", "Invariant raw UTF16 lower provider");
        Require(settings.GetExpanded("expanded") == "42", "Configuration expansion through native search provider");
        settings.Set("answer", "43");
        Require(settings.GetInt("ANSWER") == 43, "Case-insensitive update reads live value");
        var directory = Path.Combine(Path.GetTempPath(), "nini-config-invariant-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, "configuration-\u914d\u7f6e.ini");
        try
        {
            source.Save(path);
            var reloaded = new IniConfigSource(path);
            Require(reloaded.Configs["Settings"].GetInt("answer") == 43, "Actual Nini source file save/load");
            settings.Set("bad", new string ('\ud800', 1));
            try
            {
                settings.GetInt("bad");
                throw new Exception("Expected numeric format error");
            }
            catch (FormatException e)
            {
                Require(e.Message.Contains('\ud800'), "Raw UTF16 error metadata survives provider adapter");
            }
        }
        finally
        {
            File.Delete(path);
            Directory.Delete(directory);
        }

        Console.WriteLine("PASS concrete invariant Nini config numeric/culture/stream/file integration");
    }
}

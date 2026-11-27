using System;
using System.Linq;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using UUID = OpenMetaverse.UUID;

class FaceMaterial
{
    /*FIELDS*/
    /*CONSTRUCTOR*/
}
class Program
{
    static string Observe(int seed, int mode)
    {
        var input = new OSDMap();
        if (mode != 1)
        {
            string[] fields = { "NormRepeatX", "NormRepeatY", "NormOffsetX", "NormOffsetY", "NormRotation",
                                "SpecRepeatX", "SpecRepeatY", "SpecOffsetX", "SpecOffsetY", "SpecRotation" };
            for (int i = 0; i < fields.Length; ++i)
                input[fields[i]] = OSD.FromInteger((seed - 128) * 123457 + i * 97531);
            input["NormMap"] = OSD.FromUUID(new UUID("00000000-0000-0000-0000-000000000032"));
            input["SpecMap"] = OSD.FromUUID(new UUID("00000000-0000-0000-0000-000000000033"));
            var color = new OSDArray();
            foreach (int channel in new[] { seed, 255 - seed, seed ^ 0x55, 255 }) color.Add(OSD.FromInteger(channel));
            if (mode == 2) color.RemoveAt(3);
            input["SpecColor"] = color;
            foreach (string key in new[] { "SpecExp", "EnvIntensity", "DiffuseAlphaMode", "AlphaMaskCutoff" })
                input[key] = OSD.FromInteger(seed + 513);
        }
        var value = new FaceMaterial(input);
        float[] components = { value.NormalRepeatX, value.NormalRepeatY, value.NormalOffsetX, value.NormalOffsetY,
                               value.NormalRotation, value.SpecularRepeatX, value.SpecularRepeatY, value.SpecularOffsetX,
                               value.SpecularOffsetY, value.SpecularRotation };
        byte[] bytes = { value.SpecularLightColorR, value.SpecularLightColorG, value.SpecularLightColorB,
                         value.SpecularLightExponent, value.EnvironmentIntensity, value.DiffuseAlphaMode, value.AlphaMaskCutoff };
        return value.NormalMapID + "|" + value.SpecularMapID + "|"
            + string.Concat(components.Select(x => BitConverter.SingleToUInt32Bits(x).ToString("x8") + ";")) + "|"
            + string.Concat(bytes.Select(x => x + ";"));
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ').Select(int.Parse).ToArray();
            Console.WriteLine(Observe(p[0], p[1]));
        }
    }
}

using System;
using System.Globalization;
using OpenMetaverse;
class Program
{
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var f = Array.ConvertAll(line.Split(' '),
                value => BitConverter.UInt32BitsToSingle(uint.Parse(value, NumberStyles.HexNumber)));
            var result = new Vector3(f[0], f[1], f[2]) * new Quaternion(f[3], f[4], f[5], f[6]);
            Console.WriteLine(BitConverter.SingleToUInt32Bits(result.X).ToString("x8") + " " +
                              BitConverter.SingleToUInt32Bits(result.Y).ToString("x8") + " " +
                              BitConverter.SingleToUInt32Bits(result.Z).ToString("x8"));
        }
    }
}

using System;
using System.Text;
partial class Util
{
    public static Encoding UTF8 = Encoding.UTF8;
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var w = line.Split(' ');
            int size = int.Parse(w[0]), seed = int.Parse(w[1]), mode = int.Parse(w[2]),
                label = int.Parse(w[3]);
            var bytes = new byte[size];
            for (int i = 0; i < size; ++i)
            {
                if (mode == 0 || mode == 1)
                    bytes[i] = (byte)(32 + (seed + i) % 95);
                else if (mode == 2)
                    bytes[i] = (byte)(seed + i * 17);
                else if (mode == 3)
                    bytes[i] = (byte)(i % 4 == 0 ? 0 : i % 4 == 1 ? 13 : i % 4 == 2 ? 10 : 9);
                else
                    bytes[i] = new byte[] { 0xe9, 0x9b, 0xaa, 0x7e, 0x00 }[i % 5];
            }
            if (mode == 1 && size > 0)
                bytes[size - 1] = 0;
            try
            {
                var value = FieldToString(bytes, label == 0   ? ""
                                                 : label == 1 ? "field"
                                                 : label == 2 ? "雪"
                                                              : null);
                Console.WriteLine(Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant());
            }
            catch
            {
                Console.WriteLine("ERROR");
            }
        }
    }
}

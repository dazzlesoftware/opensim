using System;
using System.Reflection;
using OpenMetaverse;
class Program
{
    static void Main()
    {
        var f = new Primitive.TextureEntryFace[4];
        f[0] = new Primitive.TextureEntryFace(null);
        f[1] = new Primitive.TextureEntryFace(f[0]);
        f[2] = new Primitive.TextureEntryFace(f[1]);
        f[3] = (Primitive.TextureEntryFace)f[2].Clone();
        var attr = typeof(Primitive.TextureEntryFace)
                       .GetField("m_attributes", BindingFlags.NonPublic | BindingFlags.Instance);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int i = int.Parse(p[0]), op = int.Parse(p[1]);
            uint bits = Convert.ToUInt32(p[2], 16);
            float v = BitConverter.UInt32BitsToSingle(bits);
            var x = f[i];
            switch (op)
            {
            case 0:
                x.RepeatU = v;
                break;
            case 1:
                x.RepeatV = v;
                break;
            case 2:
                x.OffsetU = v;
                break;
            case 3:
                x.OffsetV = v;
                break;
            case 4:
                x.Rotation = v;
                break;
            case 5:
                x.Glow = v;
                break;
            case 6:
                x.Bump = (Bumpiness)(byte)bits;
                break;
            case 7:
                x.Shiny = (Shininess)(byte)bits;
                break;
            case 8:
                x.Fullbright = bits != 0;
                break;
            case 9:
                x.MediaFlags = bits != 0;
                break;
            case 10:
                x.TexMapType = (MappingType)(byte)bits;
                break;
            case 11:
                x.RGBA = new Color4 { R = v, G = v, B = v, A = v };
                break;
            case 12:
                x.DirtyFlags = (TextureAttributes)bits;
                break;
            case 13:
                f[3] = (Primitive.TextureEntryFace)x.Clone();
                break;
            case 14:
                x.TextureID = bits != 0 ? new UUID("5748decc-f629-461c-9a36-a35a221fe21f") : UUID.Zero;
                break;
            case 15:
                x.MaterialID = bits != 0 ? new UUID("5748decc-f629-461c-9a36-a35a221fe21f") : UUID.Zero;
                break;
            }
            foreach (var face in f)
            {
                Console.Write(((uint)(TextureAttributes)attr.GetValue(face)).ToString("x") + "," +
                              ((uint)face.DirtyFlags).ToString("x") + ",");
                var c = face.RGBA;
                foreach (float n in new[] { c.R, c.G, c.B, c.A, face.RepeatU, face.RepeatV, face.OffsetU,
                                            face.OffsetV, face.Rotation, face.Glow })
                    Console.Write(
                        (float.IsNaN(n) ? 0x7fc00000 : BitConverter.SingleToUInt32Bits(n)).ToString("x") +
                        ",");
                Console.Write(((byte)face.Bump).ToString("x") + "," + ((byte)face.Shiny).ToString("x") + "," +
                              (face.Fullbright ? 1 : 0) + "," + (face.MediaFlags ? 1 : 0) + "," +
                              ((byte)face.TexMapType).ToString("x") + "," + face.TextureID + "," +
                              face.MaterialID + ";");
            }
            Console.WriteLine();
        }
    }
}

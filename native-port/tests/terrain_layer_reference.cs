using System;
using System.Collections.Generic;
using OpenSim.Framework;
using OpenMetaverse;
using OpenSim.Region.ClientStack.LindenUDP;
namespace log4net
{
    public interface ILog
    {
        void Debug(object x);
        void DebugFormat(string f, params object[] a);
        void Error(object x);
        void ErrorFormat(string f, params object[] a);
        void InfoFormat(string f, params object[] a);
    }
    public class StubLogger : ILog
    {
        public void Debug(object x)
        {
        }
        public void DebugFormat(string f, params object[] a)
        {
        }
        public void Error(object x)
        {
        }
        public void ErrorFormat(string f, params object[] a)
        {
        }
        public void InfoFormat(string f, params object[] a)
        {
        }
    }
    public static class LogManager
    {
        public static StubLogger GetLogger(Type t) => new StubLogger();
    }
}
namespace OpenSim.Framework
{
    public static class Constants
    {
        public const int TerrainPatchSize = 16;
        public const uint RegionSize = 256;
    }
}
class TerrainLayerReference
{
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            int k = 0;
            int w = int.Parse(a[k++]), h = int.Parse(a[k++]), mode = int.Parse(a[k++]),
                seed = int.Parse(a[k++]), n = int.Parse(a[k++]);
            var t = new TerrainData(w, h, 0);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    float f = 21;
                    if (mode == 1)
                        f = ((x * 17 + y * 31 + seed * 13) % 397 - 198) * .25f;
                    if (mode == 2)
                        f = 21f + ((x + y + seed) % 3) * .001f;
                    if (mode == 3)
                        f = x * .125f - y * .0625f + seed;
                    t[x, y] = f;
                }
            byte type = (byte)(w > 256 || h > 256 ? 77 : 76);
            byte[] data = new byte[65536];
            BitPack bits = null;
            Action start = () =>
            {
                data = new byte[65536];
                bits = new BitPack(data, 10);
                bits.PackBits(264, 16);
                bits.PackBitsFromByte(16);
                bits.PackBitsFromByte(type);
            };
            Action finish = () =>
            {
                bits.PackBitsFromByte(97);
                Console.Write(type + ":" +
                              Convert.ToHexString(data, 10, bits.BytePos - 9).ToLowerInvariant() + ";");
            };
            start();
            for (int i = 0; i < n; i++)
            {
                int x = int.Parse(a[k++]), y = int.Parse(a[k++]);
                OpenSimTerrainCompressor.CreatePatchFromTerrainData(bits, t, x, y);
                if (bits.BytePos > 900 && i != n - 1)
                {
                    finish();
                    start();
                }
            }
            finish();
            Console.WriteLine();
        }
    }
}

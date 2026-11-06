using System;
using System.IO;
using System.Text;
using OpenMetaverse;
using OpenSim.Framework;
class ProbeWriter : TextWriter
{
    public override Encoding Encoding => Encoding.UTF8;
    int count;
    public override void WriteLine(string s)
    {
        Probe.Trace += "|LINE:" + Probe.Hex(s);
        if (++count == Probe.Failure)
            throw new Exception();
    }
}
class Probe
{
    public static string Trace;
    public static int Failure;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static string Hex(string s) =>
        s == null ? "NULL" : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Dump(AnimationSet model)
    {
        if (model == null)
            return "NULL";
        var bytes = model.ToBytes();
        string s =
            $"{model.AnimationCount}:{Convert.ToHexString(bytes).ToLowerInvariant()}:{(ReferenceEquals(bytes,AnimationSet.ZeroCountBytesReply)?1:0)}";
        foreach (var key in new[] { "a", "b", "c", "d", "new", "脚 😃" })
            s += ":" + model.GetAnimation(key) + ":" + Hex(model.GetAnimationName(key));
        return s;
    }
    static string Run(int mode, int seed, int failure)
    {
        Failure = failure;
        Trace = "";
        var old = Console.Out;
        AnimationSet model = null;
        string status = "OK";
        byte saved = AnimationSet.ZeroCountBytesReply[0];
        bool Bit(int n) => (mode & n) != 0;
        try
        {
            Console.SetOut(new ProbeWriter());
            byte[] data = new byte[Bit(131072) ? 256 : seed % 18];
            for (int j = 0; j < data.Length; j++)
                data[j] = (byte)(Bit(131072) ? j : seed * 13 + j * 37);
            if (Bit(256))
                data = Encoding.ASCII.GetBytes("version 1\ncount 1\na " + Id(1) + " Name\n");
            model = new AnimationSet(Bit(128) ? null : data);
            Trace += "|INITIAL:" + Dump(model);
            if (Bit(32768))
                AnimationSet.ZeroCountBytesReply[0] = (byte)'!';
            if (!Bit(512))
            {
                string a = Bit(1024) ? "脚 😃" : "a";
                model.SetAnimation(a, Bit(65536) ? "é\0😃" : Bit(1024) ? "説明 😃" : "A", Id(1));
                model.SetAnimation("b", Bit(2048) ? null : "B", Id(2));
                model.SetAnimation(a, "Updated", Id(3));
                if ((seed & 1) != 0)
                    model.SetAnimation(a, null, UUID.Zero);
                model.SetAnimation("c", Bit(65536) ? "é\0😃" : "C", Id(4));
                if ((seed & 2) != 0)
                    model.SetAnimation("b", null, UUID.Zero);
                model.SetAnimation("d", "", Id(5));
            }
            Trace += "|BEFORE:" + Dump(model);
            if (Bit(4096))
                model.SetAnimation(null, null, UUID.Zero);
            if (Bit(8192))
                model.GetAnimation(null);
            if (Bit(16384))
                model.GetAnimationName(null);
            AnimationSetValidator validator = anim =>
            {
                Trace += "|VAL:" + anim;
                if (Bit(1))
                    return 0;
                if (Bit(2))
                    throw new Exception();
                if (Bit(4))
                    model.SetAnimation("a", null, UUID.Zero);
                if (Bit(8))
                    model.SetAnimation("new", "New", Id(6));
                if (Bit(16))
                    model.SetAnimation("d", "Changed", Id(7));
                if (Bit(32))
                    model.SetAnimation("c", null, UUID.Zero);
                return Bit(262144)   ? 0xffffffffu
                       : Bit(524288) ? 0x80000000u
                                     : unchecked((uint)(seed * 1234567 | 0x2000));
            };
            var validated = model.Validate(Bit(64) ? null : validator);
            Trace += "|RESULT:" + validated;
        }
        catch
        {
            status = "THROW";
        }
        finally
        {
            Console.SetOut(old);
        }
        string result = status + ":" + Dump(model) + Trace;
        AnimationSet.ZeroCountBytesReply[0] = saved;
        return result;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Run(a[0], a[1], a[2]));
        }
    }
}

using System;
using System.Reflection;
using OpenMetaverse;
class InventoryItemBase
{
    public int InvType = Reference.Bit(1024) ? 10 : 7;
    public UUID Owner = Reference.Id(Reference.Bit(2048) ? 9 : 1);
    public uint CurrentPermissions = Reference.Bit(4096) ? 0u : 0x2000u;
}
class ScenePresence
{
    public UUID UUID = Reference.Id(Reference.Bit(512) ? 9 : 1);
    public bool IsDeleted = Reference.Bit(2), IsGod = Reference.Bit(128);
}
class SceneObjectPart
{
    public SceneObjectGroup ParentGroup = Reference.Bit(16) ? null : new();
    public uint GetEffectiveObjectFlags()
    {
        Reference.Step(3);
        return Reference.Bit(8192) ? 0x10000u : 0;
    }
}
class SceneObjectGroup
{
    public UUID OwnerID = Reference.Id(1);
    public bool IsDeleted = Reference.Bit(32);
    public uint EffectiveOwnerPerms = Reference.Bit(256) ? 0u : 0x80000u;
    public SceneObjectPart RootPart => new();
}
partial class Permissions
{
    bool m_bypassPermissions
    {
        get {
            Reference.Step(2);
            return Reference.Bit(64);
        }
    }
    void DebugPermissionInformation(string name) => Reference.Step(1);
    uint GetObjectPermissions(UUID id, SceneObjectGroup group, bool value)
    {
        if (!value)
            throw new Exception();
        Reference.Step(4);
        return Reference.Bit(16384) ? 0u : 0x4000u;
    }
    public bool Probe(InventoryItemBase item, ScenePresence sp,
                      SceneObjectPart part) => CanDropInObjectInv(item, sp, part);
}
class Reference
{
    public static int Mode, Failure;
    static string Trace;
    public static bool Bit(int mask) => (Mode & mask) != 0;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static void Step(int n)
    {
        Trace += "|" + n;
        if (Failure == n)
            throw new Exception();
    }
    static string Probe(int mode, int failure)
    {
        Mode = mode;
        Failure = failure;
        Trace = "";
        string result;
        try
        {
            result =
                new Permissions().Probe(Bit(8) ? null : new(), Bit(1) ? null : new(), Bit(4) ? null : new())
                    ? "1"
                    : "0";
        }
        catch
        {
            result = "THROW";
        }
        return result + Trace;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Probe(a[0], a[1]));
        }
    }
}

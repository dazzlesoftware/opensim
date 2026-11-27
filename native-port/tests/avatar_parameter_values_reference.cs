/*TYPES*/
static class ScriptBaseClass
{
    public const int TRUE = 1, FALSE = 0;
    /*CONSTANTS*/
}
static class SOPMaterialData { /*MATERIAL_ENUM*/ }
class Group { public SceneObjectPart RootPart; }
class SceneObjectPart
{
    public Group ParentGroup;
    public Vector3 AbsolutePosition;
    public Quaternion WorldRotation;
    public Quaternion GetWorldRotation() => WorldRotation;
}
class Appearance
{
    public Vector3 AvatarSize;
    public float AvatarHeight;
}
class ScenePresence
{
    public uint ParentID = 1;
    public string Name;
    public bool LegacySitOffsets;
    public Vector3 OffsetPosition, AbsolutePosition;
    public Quaternion Rotation, WorldRotation;
    public Func<Appearance> GetAppearance;
    public Appearance Appearance => GetAppearance();
    public Quaternion GetWorldRotation() => WorldRotation;
}
class World
{
    public Func<SceneObjectPart> Lookup;
    public SceneObjectPart GetSceneObjectPart(uint id) => Lookup();
}
class Api
{
    public World World = new();
    /*ZROT*/
    /*GETTER*/
}
class Program
{
    /*REAL*/
    /*CANONICAL*/
    static float RotationComponent(uint seed, int mode, int component)
    {
        if (mode == 8)
            return BitConverter.UInt32BitsToSingle(0x3f000000u | ((seed * 104729u + (uint)component * 174763u) & 0x007fffffu));
        int index = (int)((seed >> (component * 2)) & 3u);
        uint[] values = mode switch
        {
            9 => new uint[] { 0x7fc00000, 0xffc00000, 0x7f800000, 0xff800000 },
            10 => new uint[] { 0x7f800001, 0xff800001, 0x7fc12345, 0xffc54321 },
            _ => new uint[] { 0x7f7fffff, 0xff7fffff, 0x00000001, 0x80000001 }
        };
        return BitConverter.UInt32BitsToSingle(values[index]);
    }

    static string Observe(int seed, int legacy, int seatMode, int appearanceMode, int quaternionMode,
                          int code, int face, int targetLength)
    {
        string trace = "";
        var api = new Api();
        var avatar = new ScenePresence
        {
            Name = "Avatar " + seed,
            OffsetPosition = new(seed / 10f, -1.25f, 2.5f),
            AbsolutePosition = new(99.25f, -20.5f, seed / 8f),
            WorldRotation = new(.125f, .25f, .5f, 1f),
            LegacySitOffsets = legacy != 0,
            Rotation = quaternionMode switch
            {
                1 => new(0, 0, .7071f, .7071f),
                2 => new(1, 2, 3, 4),
                3 => new(0, 0, 0, 0),
                4 => new(BitConverter.UInt32BitsToSingle(0x7fc00000), 0, 0, 1),
                5 => new(float.PositiveInfinity, 0, 0, 1),
                6 => new(0, 0, 0, -2),
                7 => new(1e-20f, 0, 0, 1e-20f),
                >= 8 and <= 11 => new(RotationComponent((uint)seed, quaternionMode, 0),
                                      RotationComponent((uint)seed, quaternionMode, 1),
                                      RotationComponent((uint)seed, quaternionMode, 2),
                                      RotationComponent((uint)seed, quaternionMode, 3)),
                _ => new(0, 0, 0, 1)
            }
        };
        api.World.Lookup = () =>
        {
            trace += "S;";
            if (seatMode == 4) throw new Exception("Seat lookup failed");
            if (seatMode == 1) return null;
            return new SceneObjectPart
            {
                ParentGroup = seatMode == 2 ? null : new Group
                {
                    RootPart = seatMode == 3 ? null : new SceneObjectPart
                    {
                        AbsolutePosition = new(15.125f, 32.75f, -3.5f),
                        WorldRotation = new(.125f, -.25f, .5f, .75f)
                    }
                }
            };
        };
        avatar.GetAppearance = () =>
        {
            trace += "A;";
            if (appearanceMode == 2) throw new Exception("Appearance failed");
            return appearanceMode == 1 ? null : new Appearance
            {
                AvatarSize = new(.45f, .6f, seed / 64f),
                AvatarHeight = seed / 32f - 2f
            };
        };
        var rules = new LSLList();
        void Add(int property)
        {
            rules.Add(property);
            if ((property >= 17 && property <= 20) || property == 22 || property == 25 ||
                (property >= 36 && property <= 38)) rules.Add(face);
        }
        if (code == 0)
            foreach (int property in new[] {2,3,4,5,6,7,8,9,17,18,19,20,21,22,23,25,26,27,28,29,33,44})
                Add(property);
        else if (code == 34)
        {
            rules.Add(34);
            for (int i = 0; i < targetLength; ++i) rules.Add(i == 0 ? face : i == 1 ? 27 : 29);
        }
        else if (code != -1) Add(code);
        var result = new LSLList(new LSLString("prefix"));
        try
        {
            var tail = api.GetPrimParams(avatar, rules, ref result);
            return Canonical(result) + "|" + Canonical(tail) + "|" + trace;
        }
        catch (Exception error)
        {
            return Canonical(result) + "|!" + error.GetType().FullName + ":" + error.Message + "|" + trace;
        }
    }
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var f = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Observe(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]));
        }
    }
}

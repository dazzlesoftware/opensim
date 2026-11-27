using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using OpenMetaverse;
using LSL_Integer = LSLInteger;
using LSL_Float = LSLFloat;
using LSL_String = LSLString;
using LSL_Vector = LSLVector;
using LSL_Rotation = LSLRotation;
using LSL_List = LSLList;

struct LSLInteger
{
    public int value;
    public LSLInteger(int v) { value = v; }
    public static readonly LSLInteger One = new(1), Zero = new(0);
    public static implicit operator int(LSLInteger v) => v.value;
}
struct LSLFloat
{
    public double value;
    public LSLFloat(double v) { value = v; }
    public static LSLFloat operator +(LSLFloat v, double b) => new(v.value + b);
}
struct LSLString
{
    public string value;
    public LSLString(string v) { value = v; }
}
struct LSLVector
{
    public double x, y, z;
    public LSLVector(double a, double b, double c) { x = a; y = b; z = c; }
    public LSLVector(Vector3 v) : this(v.X, v.Y, v.Z) {}
    public static readonly LSLVector Zero = new();
}
struct LSLRotation
{
    public double x, y, z, s;
    public LSLRotation(double a, double b, double c, double d) { x = a; y = b; z = c; s = d; }
    public LSLRotation(Quaternion v) : this(v.X, v.Y, v.Z, v.W) {}
    public static readonly LSLRotation Identity = new(0, 0, 0, 1);
}
class LSLList
{
    public List<object> Values;
    public int Length => Values.Count;
    public LSLList(params object[] values) { Values = new(values); }
    public void Add(object value) { Values.Add(value); }
    public int GetIntegerItem(int index) => (int)Values[index];
    public LSLList GetSublist(int start, int end) =>
        new(Values.Skip(start).Take((end < 0 ? Length + end : end) - start + 1).ToArray());
}
static class ScriptBaseClass
{
    public const int TRUE = 1, FALSE = 0;
    /*CONSTANTS*/
}
/*ENUMS*/
class Probe { public float Ambiance, ClipDistance; public byte Flags; }
class PrimitiveBaseShape
{
    public bool SculptEntry, AnimeshEnabled, FlexiEntry, LightEntry, ProjectionEntry;
    public byte SculptType, ProfileCurve, PathCurve, PathRevolutions, PathScaleX, PathScaleY, PathShearX, PathShearY;
    public ushort ProfileBegin, ProfileEnd, ProfileHollow, PathBegin, PathEnd;
    public sbyte PathSkew, PathTwistBegin, PathTwist, PathTaperX, PathTaperY, PathRadiusOffset;
    public UUID SculptTexture, ProjectionTextureUUID;
    public int FlexiSoftness;
    public float FlexiGravity, FlexiDrag, FlexiWind, FlexiTension, FlexiForceX, FlexiForceY, FlexiForceZ;
    public float LightColorR, LightColorG, LightColorB, LightIntensity, LightRadius, LightFalloff;
    public float ProjectionFOV, ProjectionFocus, ProjectionAmbiance;
    public Probe ReflectionProbe;
    public Primitive.TextureEntry Textures;
}
class TaskInventoryItem { public int InvType; public UUID AssetID; }
class Group { public UUID UUID; public SceneObjectPart RootPart; }
class SceneObjectPart
{
    public PrimitiveBaseShape Shape = new();
    public int Material, PhysicsShapeType;
    public uint Flags;
    public string Name, Description, Text;
    public Vector3 AbsolutePosition, Scale, LocalPosition, AngularVelocity, SitTargetPosition;
    public Quaternion WorldRotation, RotationOffset, SitTargetOrientation;
    public bool IsSitTargetSet;
    public Color4 TextColor;
    public Group ParentGroup;
    public UUID OwnerID;
    public Dictionary<UUID, TaskInventoryItem> TaskInventory = new();
    public uint GetEffectiveObjectFlags() => Flags;
    public Color4 GetTextColor() => TextColor;
    /*GEOMETRY_METHODS*/
}
class FaceMaterial
{
    public UUID NormalMapID, SpecularMapID;
    public float NormalRepeatX, NormalRepeatY, NormalOffsetX, NormalOffsetY, NormalRotation;
    public float SpecularRepeatX, SpecularRepeatY, SpecularOffsetX, SpecularOffsetY, SpecularRotation;
    public byte SpecularLightColorR, SpecularLightColorG, SpecularLightColorB;
    public byte SpecularLightExponent, EnvironmentIntensity, DiffuseAlphaMode, AlphaMaskCutoff;
}
class Materials { public FaceMaterial Value; public FaceMaterial GetMaterial(UUID id) => Value; }
class Permissions
{
    public bool Allowed;
    public Action OnCheck;
    public bool CanEditObject(UUID obj, UUID owner) { OnCheck?.Invoke(); return Allowed; }
}
class World { public Permissions Permissions = new(); }
class Api
{
    public SceneObjectPart m_host;
    public Materials m_materialsModule;
    public World World = new();
    static int GetNumberOfSides(SceneObjectPart part) => part.GetNumberOfSides();
    static LSLRotation GetPartRot(SceneObjectPart part) => new(part.WorldRotation);
    static Vector3 GetPartLocalPos(SceneObjectPart part) => part.LocalPosition;
    /*GETTER_METHODS*/
}
class Program
{
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    static string Real(double value) => BitConverter.DoubleToUInt64Bits(value).ToString("x16");
    static string Canonical(LSLList list)
    {
        var result = new StringBuilder();
        foreach (var item in list.Values)
            result.Append(item switch
            {
                LSLInteger v => "i" + v.value,
                int v => "i" + v,
                LSLFloat v => "f" + Real(v.value),
                LSLString v => "s" + Convert.ToHexString(Encoding.UTF8.GetBytes(v.value)).ToLowerInvariant(),
                LSLVector v => "v" + Real(v.x) + Real(v.y) + Real(v.z),
                LSLRotation v => "r" + Real(v.x) + Real(v.y) + Real(v.z) + Real(v.s),
                _ => throw new Exception("Unexpected result type: " + item.GetType().Name)
            }).Append(';');
        return result.ToString();
    }
    static string Observe(int type, int seed, uint flags, int code, int face, int mode)
    {
        var part = new SceneObjectPart
        {
            Name = "Name " + seed, Description = "Desc<&" + seed, Text = "Text",
            TextColor = new Color4((byte)seed, (byte)((seed * 3) & 255), (byte)((seed * 7) & 255), (byte)seed),
            Material = seed % 8, PhysicsShapeType = seed % 3, Flags = flags,
            Scale = new(seed / 10f, 2.25f, -3.5f), RotationOffset = new(.5f, .25f, .125f, 1f),
            AngularVelocity = seed % 3 == 0 ? new() : seed % 3 == 1 ? new(3, 4, 0) : new(1e-7f, -1e-7f, 0),
            AbsolutePosition = new(10.25f, -20.5f, seed / 8f), LocalPosition = new(-1.125f, 2f, seed / 16f),
            WorldRotation = new(.125f, .25f, .5f, 1f),
            IsSitTargetSet = (seed & 16) != 0, SitTargetPosition = new(.25f, .5f, .75f),
            SitTargetOrientation = new(0, 0, 1, 0)
        };
        part.ParentGroup = new Group { RootPart = part };
        var s = part.Shape;
        byte[] profiles = { 1, 0, 3, 5, 0, 1, 3, 0, 0 };
        s.ProfileCurve = (byte)(profiles[type] | ((seed % 4) << 4));
        s.PathCurve = (byte)(type >= 3 && type <= 6 ? 32 : 16);
        s.SculptEntry = type >= 7;
        s.SculptType = (byte)(type == 8 ? 5 : 1);
        s.SculptTexture = Id(40);
        s.AnimeshEnabled = type == 8 && (seed & 1) != 0;
        s.ProfileBegin = (ushort)((seed % 5) * 1000); s.ProfileEnd = (ushort)((seed % 7) * 1100);
        s.ProfileHollow = (ushort)(seed * 100); s.PathBegin = (ushort)((seed % 3) * 2000);
        s.PathEnd = (ushort)((seed % 4) * 1300); s.PathTwistBegin = (sbyte)(seed - 128);
        s.PathTwist = (sbyte)(127 - seed); s.PathTaperX = (sbyte)(seed % 99 - 49);
        s.PathTaperY = (sbyte)(49 - seed % 99); s.PathRadiusOffset = (sbyte)(seed % 101 - 50);
        s.PathSkew = (sbyte)(seed % 97 - 48); s.PathRevolutions = (byte)seed;
        s.PathScaleX = s.PathShearX = (byte)seed; s.PathScaleY = s.PathShearY = (byte)(255 - seed);
        s.FlexiEntry = (seed & 1) != 0; s.FlexiSoftness = seed % 4; s.FlexiGravity = -.25f * seed;
        s.FlexiDrag = .125f * seed; s.FlexiWind = .5f; s.FlexiTension = 2.25f;
        s.FlexiForceX = 1.5f; s.FlexiForceY = -2.25f; s.FlexiForceZ = 3.125f;
        s.LightEntry = (seed & 2) != 0; s.LightColorR = seed / 255f; s.LightColorG = .25f; s.LightColorB = .75f;
        s.LightIntensity = .625f; s.LightRadius = seed / 2f; s.LightFalloff = 1.25f;
        s.ProjectionEntry = (seed & 4) != 0; s.ProjectionTextureUUID = Id(12);
        s.ProjectionFOV = 1.5f; s.ProjectionFocus = 2.5f; s.ProjectionAmbiance = -.25f;
        if ((seed & 8) != 0)
            s.ReflectionProbe = new() { Ambiance = seed / 10f, ClipDistance = -.75f, Flags = (byte)seed };
        var texture = new Primitive.TextureEntry(Id(10));
        var df = texture.DefaultTexture;
        df.RGBA = new Color4(seed / 255f, .25f, .75f, .5f);
        df.RepeatU = 1.25f; df.RepeatV = -.75f; df.OffsetU = .3333f; df.OffsetV = -.2f; df.Rotation = .75f;
        df.Bump = (Bumpiness)(seed % 32); df.Shiny = (Shininess)((seed % 4) << 6);
        df.Fullbright = (seed & 16) != 0; df.TexMapType = (MappingType)((seed % 3) * 2); df.Glow = seed / 255f;
        df.MaterialID = mode == 5 ? UUID.Zero : Id(20);
        texture.CreateFace(2).TextureID = Id(11); texture.CreateFace(2).RepeatU = .75f;
        var bytes = texture.GetBytes();
        s.Textures = new Primitive.TextureEntry(bytes, 0, bytes.Length);
        var api = new Api { m_host = part };
        api.World.Permissions.Allowed = mode == 2 || mode == 6;
        if (mode == 3)
            part.TaskInventory[Id(50)] = new TaskInventoryItem { InvType = (int)InventoryType.Texture, AssetID = Id(30) };
        if (mode != 0)
            api.m_materialsModule = new Materials { Value = mode == 4 ? null : new FaceMaterial
            {
                NormalMapID = Id(30), SpecularMapID = Id(31),
                NormalRepeatX = 1.25f, NormalRepeatY = -.75f, NormalOffsetX = .125f, NormalOffsetY = -.25f,
                NormalRotation = .75f, SpecularRepeatX = 2, SpecularRepeatY = 3,
                SpecularOffsetX = -.5f, SpecularOffsetY = .5f, SpecularRotation = -.25f,
                SpecularLightColorR = (byte)seed, SpecularLightColorG = 128, SpecularLightColorB = 255,
                SpecularLightExponent = (byte)seed, EnvironmentIntensity = (byte)(255 - seed),
                DiffuseAlphaMode = (byte)(seed % 4), AlphaMaskCutoff = (byte)seed
            }};
        if (mode == 6)
            api.World.Permissions.OnCheck = () =>
            {
                api.m_materialsModule.Value.NormalRepeatX += 1f;
                api.m_materialsModule.Value.SpecularRepeatX += 1f;
            };
        var rules = new LSLList();
        void Add(int property)
        {
            rules.Add(property);
            if ((property >= 17 && property <= 20) || property == 22 || property == 25 ||
                (property >= 36 && property <= 38)) rules.Add(face);
        }
        if (code == 0)
            foreach (int property in new[] {2,3,4,5,6,7,8,9,17,18,19,20,21,22,23,25,26,27,28,29,30,32,33,35,36,37,38,41,42,44})
                Add(property);
        else if (code == 34) rules = new LSLList(34, face, 27);
        else Add(code);
        var result = new LSLList();
        try { return Canonical(result = Read(api, part, rules, result, out var tail)) + "|" + Canonical(tail); }
        catch (Exception error) when (error.Message.Contains(" is outside the range of MAX_FACES"))
        { return Canonical(result) + "|!" + error.GetType().FullName + ":" + error.Message; }
    }
    static LSLList Read(Api api, SceneObjectPart part, LSLList rules, LSLList result, out LSLList tail)
    {
        tail = api.GetPrimParams(part, rules, ref result);
        return result;
    }
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var f = line.Split(' ');
            Console.WriteLine(Observe(int.Parse(f[0]), int.Parse(f[1]), uint.Parse(f[2]),
                                      int.Parse(f[3]), int.Parse(f[4]), int.Parse(f[5])));
        }
    }
}

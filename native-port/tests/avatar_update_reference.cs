using System;
using OpenMetaverse;
using OpenSim.Region.ClientStack.LindenUDP;
namespace OpenSim.Framework
{
    public static class Util
    {
        public unsafe static int osUTF8Getbytes(string s, byte *b, int n, bool terminate)
        {
            throw new NotSupportedException("unused short-string fixture path");
        }
    }
}
class Appearance
{
    public Vector3 AvatarSize;
}
class Group
{
    public uint LocalId;
}
class SceneObjectPart
{
    public Group ParentGroup;
}
class ScenePresence
{
    public uint LocalId;
    public UUID UUID;
    public Appearance Appearance = new();
    public Vector4 CollisionPlane;
    public Vector3 OffsetPosition, Velocity;
    public Quaternion Rotation;
    public bool Flying, IsSatOnObject, HideTitle;
    public SceneObjectPart ParentPart;
    public string Firstname, Lastname, Grouptitle;
}
partial class AvatarUpdateReference
{
    static ScenePresence Avatar(int seed, int mode)
    {
        var a = new ScenePresence();
        a.UUID = new UUID("00112233-4455-6677-8899-aabbccddeeff");
        a.LocalId = (uint)(720001 + seed);
        if ((mode & 2) != 0)
            a.ParentPart = new SceneObjectPart { ParentGroup = new Group { LocalId = 900001 } };
        a.OffsetPosition = new Vector3(seed * .25f, -seed * .125f, 23f);
        a.Velocity = new Vector3(seed * .03125f, 0, 1);
        a.Appearance.AvatarSize = new Vector3(.45f, .6f, 1.9f + seed * .001f);
        a.Rotation = new Quaternion(seed * .01f, -.2f, .3f, (mode & 4) != 0 ? -.4f : .4f);
        if ((mode & 8) != 0)
            a.Rotation = new Quaternion(0, 0, 0, 0);
        if ((mode & 16) != 0)
            a.Rotation = new Quaternion(.01f, .02f, .03f, .99995f);
        a.Flying = (mode & 1) != 0;
        a.IsSatOnObject = (mode & 2) != 0;
        a.HideTitle = (mode & 32) != 0;
        a.CollisionPlane = new Vector4(0, 0, 1, -23);
        a.Firstname = "First" + seed;
        a.Lastname = "Last";
        a.Grouptitle = "Title";
        if ((mode & 64) != 0)
        {
            a.Firstname += "é";
            a.Lastname = "雪";
            a.Grouptitle = "世界";
        }
        if ((mode & 128) != 0)
            a.Grouptitle += "\0";
        if ((mode & 256) != 0)
        {
            a.Appearance.AvatarSize = new Vector3(-0f, 0, -0f);
            a.OffsetPosition = new Vector3(-0f, 0, 0);
            a.Velocity = new Vector3(0, -0f, 0);
            a.CollisionPlane = new Vector4(-0f, 0, 0, 0);
        }
        return a;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var args = line.Split(' ');
            int seed = int.Parse(args[0]), mode = int.Parse(args[1]);
            byte[] data = new byte[65536];
            data[0] = 0xc0;
            data[6] = 12;
            var zc = new LLUDPZeroEncoder(data);
            zc.Position = 7;
            zc.AddUInt64(0x123456789abcdef0ul);
            zc.AddUInt16(Utils.FloatZeroOneToushort(seed * .03125f));
            zc.AddByte(1);
            CreateAvatarUpdateBlock(Avatar(seed, mode), zc);
            int n = zc.Finish();
            Console.WriteLine(Convert.ToHexString(data, 0, n).ToLowerInvariant());
        }
    }
}

using System;
using System.Collections.Generic;
using OpenMetaverse;
class Constants
{
    public const int RegionSize = 256;
}
class PrimitiveBaseShape
{
}
class Util
{
    public static T Clamp<T>(T x, T min, T max)
        where T : IComparable<T>
    {
        if (x.CompareTo(max) > 0)
            return max;
        if (x.CompareTo(min) < 0)
            return min;
        return x;
    }
}
class PhysicsActor
{
    public virtual Vector3 Position { get; set; }
    public virtual Vector3 Velocity { get; set; }
    public virtual Vector3 Size { get; set; }
    public virtual Vector3 Acceleration { get; set; }
    public virtual Vector3 RotationalVelocity { get; set; }
    public virtual bool Flying { get; set; }
    public virtual bool IsColliding { get; set; }
    public int Updates;
    public Action Update;
    public void RequestPhysicsterseUpdate()
    {
        Updates++;
        Update?.Invoke();
    }
}
class POSPrim : PhysicsActor
{
    public Quaternion Orientation;
}
partial class POSCharacter : PhysicsActor
{
    private Vector3 _position, _acceleration, m_rotationalVelocity = Vector3.Zero;
    public Vector3 _velocity, _target_velocity = Vector3.Zero, _size = Vector3.Zero;
    private bool flying, isColliding;
}
partial class PosPhysicsReference
{
    private List<POSCharacter> _characters = new();
    private List<POSPrim> _prims = new();
    private float[] _heightMap;
    private const float gravity = -9.8f;
    static string H(float f) => BitConverter.SingleToUInt32Bits(f).ToString("x");
    static string V(Vector3 v) => H(v.X) + "," + H(v.Y) + "," + H(v.Z);
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            int seed = int.Parse(a[0]), mode = int.Parse(a[1]);
            var scene = new PosPhysicsReference();
            var heights = new float[65536];
            for (int y = 0; y < 256; y++)
                for (int x = 0; x < 256; x++)
                    heights[y * 256 + x] = 21f + ((x * 17 + y * 31) % 23) * .03125f;
            scene.SetTerrain(heights);
            Vector3 p = new Vector3(seed * .25f, 2, (mode & 4) != 0 ? 21f : 25f);
            if ((mode & 8) != 0)
                p.X = -.1f;
            if ((mode & 16) != 0)
                p.Y = 255.99f;
            var c = (POSCharacter)scene.AddAvatar("test", p, new Vector3(1, 2, 3),
                                                  new Vector3(.45f, .6f, 1.9f), (mode & 32) != 0);
            if ((mode & 64) != 0)
                c.Size = new Vector3(.45f, .6f, 1.9f);
            if ((mode & 128) != 0)
                c.Velocity = new Vector3(2, -3, 4);
            if ((mode & 256) != 0)
                scene.AddPrimShape(
                    "prim", null, p, new Vector3(1, 1, 2),
                    (mode & 512) != 0 ? new Quaternion(.1f, .2f, .3f, .4f) : Quaternion.Identity, false, 1);
            float dt = mode % 4 == 0 ? 0 : mode % 4 == 1 ? .016f : mode % 4 == 2 ? .25f : -.125f;
            int callbacks = 0;
            c.Update = () => callbacks++;
            float fps = 0;
            string error = "";
            try
            {
                for (int i = 0; i < ((mode & 1024) != 0 ? 5 : 1); i++)
                    fps = scene.Simulate(dt);
            }
            catch (IndexOutOfRangeException)
            {
                error = "EX;";
            }
            Console.WriteLine(error + V(c.Position) + "|" + V(c._velocity) + "|" + V(c._target_velocity) +
                              "|" + V(c.Size) + "|" + (c.IsColliding ? 1 : 0) + "|" + c.Updates + "|" +
                              callbacks + "|" + H(fps));
        }
    }
}

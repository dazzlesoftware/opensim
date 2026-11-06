using System;
using System.Collections.Generic;
using OpenMetaverse;
class Constants
{
    public const float MinSimulationHeight = -100;
}
class ContactResult
{
    public uint ConsumerID;
    public Vector3 Pos, Normal;
}
class EntityIntersection
{
    public bool HitTF;
    public Vector3 ipoint, normal, AAfaceNormal;
}
class SceneObjectPart
{
    public UUID UUID;
    public RezLocationReference Host;
    public EntityIntersection TestIntersectionOBB(Ray ray, Quaternion q, bool front, bool center)
    {
        Host.Trace += "O:" + RezLocationReference.V(ray.Origin) + ":" +
                      RezLocationReference.V(ray.Direction) + ":" + RezLocationReference.H(q.W) + ":" +
                      (front ? 1 : 0) + ":" + (center ? 1 : 0) + ";";
        return Host.Intersection((Host.Mode & 64) != 0);
    }
}
class Settings
{
    public double WaterHeight = 20;
}
class Info
{
    public Settings RegionSettings = new();
}
class Graph
{
    public RezLocationReference Host;
    public EntityIntersection GetClosestIntersectingPrim(Ray ray, bool front, bool center)
    {
        Host.Trace += "C:" + RezLocationReference.V(ray.Origin) + ":" +
                      RezLocationReference.V(ray.Direction) + ":" + (front ? 1 : 0) + ":" + (center ? 1 : 0) +
                      ";";
        return Host.Intersection((Host.Mode & 128) != 0);
    }
}
partial class RezLocationReference
{
    public int Mode, Seed;
    public string Trace = "";
    public UUID Target;
    public Info RegionInfo = new();
    public Graph m_sceneGraph;
    public static string H(float f) => BitConverter.SingleToUInt32Bits(f).ToString("x");
    public static string V(Vector3 v) => H(v.X) + "," + H(v.Y) + "," + H(v.Z);
    public EntityIntersection Intersection(bool hit) => new EntityIntersection {
        HitTF = hit, ipoint = new Vector3(Seed * .125f, 2, 15), normal = new Vector3(.25f, -.5f, 1),
        AAfaceNormal = (Mode & 2048) != 0   ? Vector3.Zero
                       : (Mode & 1024) != 0 ? new Vector3(1, 0, 0)
                                            : new Vector3(1, 1, (Mode & 512) != 0 ? 1 : 0)
    };
    bool SupportsRayCastFiltered() => (Mode & 8) != 0;
    object RayCastFiltered(Vector3 start, Vector3 direction, float length, int count, RayFilterFlags flags)
    {
        Trace +=
            "P:" + V(start) + ":" + V(direction) + ":" + H(length) + ":" + count + ":" + (ushort)flags + ";";
        if ((Mode & 16) == 0)
            return null;
        return new List<ContactResult> {
            new ContactResult { ConsumerID = 11, Pos = new Vector3(1, 2, Seed * .25f),
                                Normal = new Vector3(1, 0, 0) },
            new ContactResult { ConsumerID = 0, Pos = new Vector3(4, 5, 16), Normal = new Vector3(0, 0, 1) },
            new ContactResult { ConsumerID = 22, Pos = new Vector3(7, 8, 18), Normal = new Vector3(0, 1, 0) }
        };
    }
    SceneObjectPart GetSceneObjectPart(uint id)
    {
        Trace += "L:" + id + ";";
        if (id == 0)
            return null;
        return new SceneObjectPart { UUID = id == 22 && (Mode & 128) != 0
                                                ? Target
                                                : new UUID("11223344-5566-7788-99aa-bbccddeeff00"),
                                     Host = this };
    }
    SceneObjectPart GetSceneObjectPart(UUID id)
    {
        Trace += "T;";
        return (Mode & 32) != 0 ? new SceneObjectPart { UUID = id, Host = this } : null;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            int seed = int.Parse(a[0]), mode = int.Parse(a[1]);
            var host = new RezLocationReference { Seed = seed, Mode = mode };
            host.m_sceneGraph = new Graph { Host = host };
            Vector3 start = new Vector3(seed * .25f, 3, 30), end = new Vector3(seed * .5f, -7, 23),
                    scale = new Vector3(.5f, 2, 3);
            if ((mode & 2) != 0)
                end = start;
            if ((mode & 4) != 0)
                host.Target = new UUID("00112233-4455-6677-8899-aabbccddeeff");
            if ((mode & 256) != 0)
                end.Z = 10;
            if ((mode & 512) != 0)
                scale = new Vector3(-2, -3, -4);
            var p = host.GetNewRezLocation(start, end, host.Target, Quaternion.Identity, (byte)(mode & 255),
                                           (byte)((mode & 1) != 0 ? 1 : 2), (mode & 2) != 0, scale,
                                           (mode & 4) != 0);
            Console.WriteLine(V(p) + "|" + host.Trace);
        }
    }
}

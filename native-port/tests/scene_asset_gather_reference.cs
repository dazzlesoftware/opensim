using System;
using System.Collections.Generic;
using System.Linq;
using OpenMetaverse;
class SceneSettings
{
    public int Tag;
    UUID T(int i)
    {
        ProbeScene.Hit(32, i);
        return Probe.Id(1 + Tag * 20 + i);
    }
    public UUID TerrainTexture1 => T(0);
    public UUID TerrainTexture2 => T(1);
    public UUID TerrainTexture3 => T(2);
    public UUID TerrainTexture4 => T(3);
    public UUID TerrainPBR1 => T(4);
    public UUID TerrainPBR2 => T(5);
    public UUID TerrainPBR3 => T(6);
    public UUID TerrainPBR4 => T(7);
    public UUID TerrainImageID => T(8);
}
class SceneInfo
{
    public int Tag;
    public SceneSettings Settings;
    public SceneSettings RegionSettings
    {
        get {
            ProbeScene.Hit(31, Tag);
            return Settings;
        }
    }
    public UUID RegionID
    {
        get {
            ProbeScene.Hit(49, Tag);
            return Probe.Id(70 + Tag);
        }
    }
}
class SceneEnvironment
{
    public int Tag;
    public void GatherAssets(IDictionary<UUID, sbyte> ids)
    {
        ProbeScene.Hit(34, Tag);
        ids[Probe.Id(20 + Tag)] = 0;
        ids[UUID.Zero] = 0;
    }
}
class LandData
{
    public int Tag;
    public SceneEnvironment Env;
    public SceneEnvironment Environment
    {
        get {
            ProbeScene.Hit(38, Tag);
            return Env;
        }
    }
}
interface ILandObject
{
    LandData LandData { get; }
}
class Parcel : ILandObject
{
    public LandData Data;
    public int Tag;
    public LandData LandData
    {
        get {
            ProbeScene.Hit(37, Tag);
            return Data;
        }
    }
}
class LandChannel
{
    public int Tag;
    public List<ILandObject> List;
    public List<ILandObject> AllParcels()
    {
        ProbeScene.Hit(36, Tag);
        return List;
    }
}
class EntityRegistry
{
    public int Tag;
    public EntityBase[] List;
    public EntityBase[] GetEntities()
    {
        ProbeScene.Hit(40, Tag);
        return List;
    }
}
partial class AvatarAppearance
{
    public Primitive.TextureEntry T;
    public Primitive.TextureEntry Texture
    {
        get {
            ProbeScene.Hit(44, 0);
            return T;
        }
    }
}
class ScenePresence : EntityBase
{
    public AvatarAppearance A;
    public bool IsChildAgent
    {
        get {
            ProbeScene.Hit(41, 0);
            return ProbeScene.Profile == 15;
        }
    }
    public bool IsDeleted
    {
        get {
            ProbeScene.Hit(42, 0);
            return ProbeScene.Profile == 16;
        }
    }
    public AvatarAppearance Appearance
    {
        get {
            ProbeScene.Hit(43, 0);
            return A;
        }
    }
}
class Scene
{
    public int Tag;
    public SceneInfo Info;
    public SceneEnvironment Env;
    public LandChannel Land;
    public EntityRegistry Reg;
    public SceneInfo RegionInfo
    {
        get {
            ProbeScene.Hit(30, Tag);
            return Info;
        }
    }
    public SceneEnvironment RegionEnvironment
    {
        get {
            ProbeScene.Hit(33, Tag);
            return Env;
        }
    }
    public LandChannel LandChannel
    {
        get {
            ProbeScene.Hit(35, Tag);
            return Land;
        }
    }
    public EntityRegistry Entities
    {
        get {
            ProbeScene.Hit(39, Tag);
            return Reg;
        }
    }
}
class SceneLogger
{
    public void Info(string text)
    {
        Probe.Log("I:" + text);
        ProbeScene.Hit(48, 0);
    }
}
partial class FlotsamAssetCache
{
    SceneLogger m_log = new();
    IAssetService m_AssetService = new Service();
    List<Scene> m_Scenes;
    bool m_cleanupRunning => ProbeScene.RunningCheck();
    void StampRegionStatusFile(UUID id)
    {
        ProbeScene.Hit(47, int.Parse(Probe.Label(id)));
    }
    public FlotsamAssetCache(List<Scene> scenes)
    {
        m_Scenes = scenes;
    }
    public Dictionary<UUID, sbyte> Run() => GatherSceneAssets();
}
static class ProbeScene
{
    public static bool Active, Running;
    public static int Profile;
    static int CancelPoint, CancelNth, CancelSeen;
    public static void Hit(int point, int tag)
    {
        if (!Active)
            return;
        Probe.Log("S:" + point + ":" + tag);
        Probe.Hook(200 + point);
        if (point == CancelPoint && ++CancelSeen == CancelNth)
            Running = false;
    }
    public static bool RunningCheck()
    {
        Hit(45, 0);
        return Running;
    }
    public static string Call(int profile, int cancel, int nth)
    {
        Profile = profile;
        CancelPoint = cancel;
        CancelNth = nth;
        CancelSeen = 0;
        Running = true;
        var obj = ProbeLive.Build(profile == 13 ? 1 : 0, 3, 0, 1);
        if (profile == 25)
            ProbeLive.QueueMany();
        var scenes = new List<Scene>();
        for (int i = 0; i < 2; i++)
        {
            var texture = new Primitive.TextureEntry(Probe.Id(90));
            foreach (var index in AvatarAppearance.BAKE_INDICES)
                texture.CreateFace(index).TextureID = Probe.Id(80 + index);
            var avatar = new ScenePresence { A = new AvatarAppearance { T = texture } };
            var scene = new Scene {
                Tag = i, Info = new SceneInfo { Tag = i, Settings = new SceneSettings { Tag = i } },
                Env = new SceneEnvironment { Tag = i },
                Land =
                    new LandChannel { Tag = i,
                                      List = new() { new Parcel {
                                          Tag = i,
                                          Data = new LandData { Tag = i,
                                                                Env = new SceneEnvironment { Tag = 10 + i } }
                                      } } },
                Reg = new EntityRegistry { Tag = i,
                                           List = new EntityBase[] { obj, avatar, new EntityBase(), null } }
            };
            scenes.Add(scene);
            if (i == 0)
                switch (profile)
                {
                case 2:
                    scenes[0] = null;
                    break;
                case 3:
                    scene.Info = null;
                    break;
                case 4:
                    scene.Info.Settings = null;
                    break;
                case 5:
                    scene.Env = null;
                    break;
                case 6:
                    scene.Land = null;
                    break;
                case 7:
                    scene.Land.List[0] = null;
                    break;
                case 8:
                    ((Parcel)scene.Land.List[0]).Data = null;
                    break;
                case 9:
                    ((Parcel)scene.Land.List[0]).Data.Env = null;
                    break;
                case 10:
                    scene.Reg = null;
                    break;
                case 11:
                    scene.Reg.List = null;
                    break;
                case 12:
                    scene.Reg.List = Array.Empty<EntityBase>();
                    break;
                case 14:
                    scene.Reg.List[0] = null;
                    break;
                case 17:
                    avatar.A = null;
                    break;
                case 18:
                    avatar.A.T = null;
                    break;
                case 19:
                    texture.FaceTextures = null;
                    break;
                case 20:
                    Array.Resize(ref texture.FaceTextures, 9);
                    break;
                case 21:
                    texture.FaceTextures = Array.Empty<Primitive.TextureEntryFace>();
                    break;
                case 22:
                    foreach (var face in texture.FaceTextures)
                        if (face != null)
                            face.TextureID = AppearanceManager.DEFAULT_AVATAR_TEXTURE;
                    break;
                case 23:
                    foreach (var face in texture.FaceTextures)
                        if (face != null)
                            face.TextureID = UUID.Zero;
                    break;
                case 24:
                    texture.FaceTextures[8] = null;
                    break;
                }
        }
        if (profile == 1)
            scenes.Clear();
        Active = ProbeLive.Active = true;
        try
        {
            var ids = new FlotsamAssetCache(scenes).Run();
            return "scene:" + string.Join(",", ids.Select(k => Probe.Label(k.Key) + ":" + k.Value)) + ":" +
                   (Running ? 1 : 0);
        }
        finally
        {
            Active = ProbeLive.Active = false;
        }
    }
}
static class Thread
{
    public static void Sleep(int ms)
    {
        ProbeScene.Hit(46, ms);
    }
}

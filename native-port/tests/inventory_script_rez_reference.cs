using System;
using OpenMetaverse;
using UUID = ControlledUuid;
readonly struct ControlledUuid
{
    readonly OpenMetaverse.UUID value;
    public ControlledUuid(string s)
    {
        value = new(s);
    }
    ControlledUuid(OpenMetaverse.UUID v)
    {
        value = v;
    }
    public static ControlledUuid Zero => default;
    public static ControlledUuid Random()
    {
        RezReference.Step(1, "NEW");
        return RezReference.Id(3);
    }
    public bool IsZero() => value == OpenMetaverse.UUID.Zero;
    public bool NotEqual(ControlledUuid other) => value != other.value;
    public override string ToString() => value.ToString();
    public static ControlledUuid ParseCreator(string s)
    {
        OpenMetaverse.UUID.TryParse(s, out var id);
        return new(id);
    }
}
class InventoryItemBase
{
    public UUID ID, AssetID, Owner, GroupID;
    public string Name = "Script", Description = "Description", CreatorId;
    public UUID CreatorIdAsUuid => ControlledUuid.ParseCreator(CreatorId);
    public int AssetType = 10, InvType = 10, CreationDate = 999;
    public uint Flags = 0x4000, BasePermissions = 0x9e000, CurrentPermissions = 0x8e000,
                EveryOnePermissions = 0x8a000, GroupPermissions = 0x84000, NextPermissions = 0x82000;
}
class TaskInventoryItem
{
    public UUID ItemID, AssetID, OwnerID, CreatorID, LastOwnerID, GroupID, ParentID, ParentPartID;
    public string Name, Description;
    public int Type, InvType;
    public uint BasePermissions, CurrentPermissions, EveryonePermissions, GroupPermissions, NextPermissions,
        Flags, CreationDate;
    public bool ScriptRunning = true;
    public TaskInventoryItem()
    {
        RezReference.Step(7, "TIME");
        CreationDate = 111;
    }
}
interface IClientAPI
{
    UUID AgentId { get; }
}
class Client : IClientAPI
{
    public UUID AgentId => RezReference.Id(1);
}
class Inventory
{
    public InventoryItemBase GetItem(UUID a, UUID i)
    {
        RezReference.Step(2, "GET:" + a + ":" + i);
        return (RezReference.Mode & 1) != 0 ? null : RezReference.Source();
    }
}
class LibraryRoot
{
    public InventoryItemBase FindItem(UUID i)
    {
        RezReference.Step(3, "LIB:" + i);
        return (RezReference.Mode & 4) != 0 ? null : RezReference.Source();
    }
}
class Library
{
    public LibraryRoot LibraryRootFolder = new();
}
class Permissions
{
    public bool CanEditObjectInventory(UUID p, UUID a)
    {
        RezReference.Step(5, "EDIT:" + p + ":" + a);
        return (RezReference.Mode & 16) == 0;
    }
    public bool PropagatePermissions()
    {
        RezReference.Step(8, "PROPAGATE");
        return (RezReference.Mode & 64) != 0;
    }
}
class Logger
{
    public void ErrorFormat(string s, params object[] args) => RezReference.Step(17, "LOG");
    public void Error(string s) => RezReference.Step(17, "LOG");
}
class Events
{
    public void TriggerNewScript(UUID a, SceneObjectPart p,
                                 UUID i) => RezReference.Step(13, "EVENT:" + a + ":" + p.UUID + ":" + i);
}
class PartInventory
{
    public SceneObjectPart Part;
    public void AddInventoryItem(TaskInventoryItem t, bool allowed)
    {
        RezReference.Step(10, "INSERT:" + Part.UUID + ":" + (allowed ? 1 : 0) + ":" + RezReference.Task(t));
    }
    public bool CreateScriptInstance(UUID i, int start, bool post, string engine, int state)
    {
        RezReference.Step(12, "COMPILE:" + Part.UUID + ":" + i + ":" + start + ":" + (post ? 1 : 0) + ":" +
                                  engine + ":" + state);
        return true;
    }
}
class SceneObjectPart
{
    public UUID UUID = RezReference.Id(6),
                OwnerID = (RezReference.Mode & 128) != 0 ? RezReference.Id(9) : RezReference.Id(1);
    public SceneObjectGroup ParentGroup;
    public PartInventory Inventory;
    public SceneObjectPart()
    {
        Inventory = new() { Part = this };
    }
    public uint GetEffectiveObjectFlags()
    {
        RezReference.Step(9, "FLAGS");
        return (RezReference.Mode & 512) != 0 ? 0x10000u : 0;
    }
    public void SendPropertiesToClient(IClientAPI c) => RezReference.Step(15,
                                                                          "PROPS:" + UUID + ":" + c.AgentId);
}
partial class SceneObjectGroup
{
    public string Name = "group";
    public UUID UUID = RezReference.Id(6);
    public Scene m_scene;
    static Logger m_log = new();
    public SceneObjectPart RootPart;
    public SceneObjectPart GetPart(uint local)
    {
        RezReference.Step(6, "GROUP:" + local);
        return (RezReference.Mode & 32) != 0 ? null : RootPart;
    }
    public void InvalidateDeepEffectivePerms() => RezReference.Step(11, "INVALIDATE");
    public void ResumeScripts() => RezReference.Step(14, "RESUME");
}
partial class Scene
{
    public Inventory InventoryService = new();
    public Library LibraryService = (RezReference.Mode & 2) != 0 ? null : new();
    public Permissions Permissions = new();
    public Events EventManager = new();
    Logger m_log = new();
    public string DefaultScriptEngine = "YEngine";
    public SceneObjectPart MakePart()
    {
        var p = new SceneObjectPart();
        p.ParentGroup = new() { RootPart = p, m_scene = this };
        return p;
    }
    public SceneObjectPart GetSceneObjectPart(uint local)
    {
        RezReference.Step(4, "PART:" + local);
        return (RezReference.Mode & 8) != 0 ? null : MakePart();
    }
    public SceneObjectPart RezNewScript(UUID a, InventoryItemBase row)
    {
        RezReference.Step(16, "REZNEW:" + a + ":" + row.ID);
        return (RezReference.Mode & 8) != 0 ? null : MakePart();
    }
}
class RezReference
{
    public static int Mode, Failure;
    static string Trace;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static void Step(int p, string s)
    {
        Trace += "|" + s;
        if (p == Failure)
            throw new Exception();
    }
    public static InventoryItemBase Source() => new() {
        ID = Id(2),
        AssetID = Id(4),
        Owner = Id(7),
        AssetType = (Mode & 256) != 0 ? 7 : 10,
        InvType = (Mode & 256) != 0 ? 7 : 10,
        CurrentPermissions = (Mode & 1024) != 0 ? 0x86000u : 0x8e000u,
        Flags = (Mode & 512) != 0 ? 0x80004000u : 0x4000u,
        GroupID = Id(8),
        CreatorId = Id(5).ToString() + ((Mode & 2048) != 0 ? ";profile;name" : "")
    };
    public static string Task(TaskInventoryItem i) =>
        i.ItemID + ":" + i.AssetID + ":" + i.OwnerID + ":" + i.CreatorID + ":" + i.LastOwnerID +
        ":" + i.GroupID + ":" + i.BasePermissions + ":" + i.CurrentPermissions + ":" + i.EveryonePermissions
        + ":" + i.GroupPermissions + ":" + i.NextPermissions + ":" + i.Flags + ":" + i.CreationDate +
        ":" + i.Type + ":" + i.InvType + ":" + i.Name + ":" + i.Description + ":" + i.ParentID +
        ":" + i.ParentPartID + ":" + (i.ScriptRunning ? 1 : 0);
    static string Probe(int kind, int mode, int failure)
    {
        Mode = mode;
        Failure = failure;
        Trace = "";
        string status = "OK";
        try
        {
            var scene = new Scene();
            if (kind == 0)
            {
                var wire = Source();
                if ((mode & 4096) != 0)
                    wire.ID = UUID.Zero;
                scene.RezScript(new Client(), wire, Id(10), 123);
            }
            else if (kind == 1)
                status = scene.RezScriptFromAgentInventory(Id(1), Id(2), 123) != null ? "YES" : "NO";
            else
            {
                var part = scene.MakePart();
                status = part.ParentGroup.AddInventoryItem(
                             Id(1), 123, Source(), (mode & 1024) != 0 ? UUID.Zero : Id(3), (mode & 256) == 0)
                             ? "YES"
                             : "NO";
            }
        }
        catch
        {
            status = "THROW";
        }
        return status + Trace;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Probe(a[0], a[1], a[2]));
        }
    }
}

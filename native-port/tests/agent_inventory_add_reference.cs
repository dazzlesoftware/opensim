using System;
using OpenMetaverse;
partial class InventoryItemBase
{
    public UUID ID, Owner, Folder;
    public int AssetType;
    public string Name = "Item", m_creatorId, m_creatorData;
    public string CreatorId
    {
        get => m_creatorId;
        set => m_creatorId = value;
    }
    public string CreatorData
    {
        get => m_creatorData;
        set => m_creatorData = value;
    }
}
class InventoryFolderBase
{
    public UUID ID, Owner, ParentID;
    public string Name;
    public short Type;
}
interface IClientAPI
{
    bool IsActive { get; }
    void SendRemoveInventoryItems(UUID[] ids);
    void SendBulkUpdateInventory(InventoryFolderBase[] folders, InventoryItemBase[] items);
    void SendAgentAlertMessage(string text, bool modal);
    void SendInventoryItemCreateUpdate(InventoryItemBase item, uint callback);
}
class Client : IClientAPI
{
    public bool IsActive => (AddReference.Mode & 512) == 0;
    public void SendRemoveInventoryItems(UUID[] ids) => AddReference.Step(11, "REMOVE:" + ids[0]);
    public void SendBulkUpdateInventory(InventoryFolderBase[] f, InventoryItemBase[] items) =>
        AddReference.Step(12, "BULK:" + items[0].Owner + ":" + items[0].Folder);
    public void SendAgentAlertMessage(string s,
                                      bool m) => AddReference.Step(13, "ALERT:" + s + ":" + (m ? 1 : 0));
    public void SendInventoryItemCreateUpdate(InventoryItemBase i,
                                              uint c) => AddReference.Step(14, "CREATE:" + i.Owner + ":" +
                                                                                   i.Folder + ":" + c);
}
class ScenePresence
{
    public IClientAPI ControllingClient = (AddReference.Mode & 256) != 0 ? null : new Client();
}
class Inventory
{
    public bool AddItem(InventoryItemBase i)
    {
        AddReference.Adds++;
        AddReference.Step(1, "ADD" + AddReference.Adds + ":" + i.Owner + ":" + i.Folder + ":" +
                                 i.CreatorIdentification);
        if ((AddReference.Mode & 2048) != 0 && AddReference.Adds == 1)
            i.Folder = AddReference.Id(77);
        return AddReference.Adds == 1 ? (AddReference.Mode & 2) == 0 : (AddReference.Mode & 4) == 0;
    }
    public InventoryFolderBase GetFolderForType(UUID owner, FolderType type)
    {
        AddReference.Step(2, "SYSTEM:" + owner + ":" + (int)type);
        return (AddReference.Mode & 8) != 0 ? null : AddReference.SystemFolder();
    }
    public InventoryFolderBase GetRootFolder(UUID owner)
    {
        AddReference.Step(3, "ROOT:" + owner);
        return (AddReference.Mode & 16) != 0
                   ? null
                   : new() { Name = "Root", ID = AddReference.Id(7), Owner = AddReference.Id(13),
                             ParentID = AddReference.Id(14) };
    }
    public InventoryFolderBase GetFolder(UUID owner, UUID key)
    {
        AddReference.Step(10, "PARENT:" + owner + ":" + key);
        return (AddReference.Mode & 1024) != 0 ? null : new() { Name = "Parent" };
    }
}
class Permissions
{
    public bool IsGod(UUID owner)
    {
        AddReference.Step(5, "GOD:" + owner);
        return (AddReference.Mode & 32) != 0;
    }
}
class Events
{
    public void TriggerOnNewInventoryItemUploadComplete(InventoryItemBase i, int level)
    {
        AddReference.Step(6, "UPLOAD:" + i.Owner + ":" + level);
        if ((AddReference.Mode & 4096) != 0)
            i.Owner = AddReference.Id(88);
    }
}
class Logger
{
    public void Debug(string s) => AddReference.Step(7, "DEBUG");
    public void Warn(string s) => AddReference.Step(8, "WARN");
}
partial class Scene
{
    Inventory InventoryService = new();
    Permissions Permissions = new();
    Events EventManager = new();
    Logger m_log = new();
    ScenePresence GetScenePresence(UUID owner)
    {
        AddReference.Step(9, "PRESENCE:" + owner);
        return (AddReference.Mode & 128) != 0 ? null : new();
    }
    public void Placement(InventoryItemBase i) => ChangePlacement(i, AddReference.SystemFolder());
}
class AddReference
{
    public static int Mode, Failure, Adds;
    static string Trace;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static void Step(int p, string s)
    {
        Trace += "|" + s;
        if (p == Failure)
            throw new Exception();
    }
    public static InventoryFolderBase SystemFolder() => new() { Name = "Fallback", ID = Id(6), Owner = Id(11),
                                                                ParentID = Id(12) };
    static string Probe(int kind, int mode, int asset, int failure)
    {
        Mode = mode;
        Failure = failure;
        Adds = 0;
        Trace = "";
        var value = new InventoryItemBase { ID = Id(1),
                                            Owner = Id(2),
                                            Folder = (mode & 1) != 0 ? UUID.Zero : Id(3),
                                            AssetType = asset,
                                            CreatorId = (mode & 32768) != 0 ? "invalid" : Id(4).ToString(),
                                            CreatorData = (mode & 8192) != 0    ? null
                                                          : (mode & 16384) != 0 ? ""
                                                                                : "profile;creator" };
        string result = "VOID";
        try
        {
            var scene = new Scene();
            if (kind == 0)
                result = scene.AddInventoryItem(value, (mode & 64) == 0) ? "TRUE" : "FALSE";
            else if (kind == 1)
                scene.AddInventoryItem(new Client(), value);
            else
                scene.Placement(value);
        }
        catch
        {
            result = "THROW";
        }
        return result + ":" + Adds + ":" + value.Owner + ":" + value.Folder + ":" + value.CreatorId + Trace;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Probe(a[0], a[1], a[2], a[3]));
        }
    }
}

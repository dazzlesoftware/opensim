using System;
using System.Collections.Generic;
using System.Text;
using OpenMetaverse;
class TaskInventoryItem
{
    public UUID ItemID, AssetID, CreatorID, LastOwnerID, OwnerID, GroupID;
    public uint CreationDate, Flags, BasePermissions, CurrentPermissions, GroupPermissions,
        EveryonePermissions, NextPermissions;
    public int Type, InvType;
    public string Name, Description;
}
class Items
{
    public List<TaskInventoryItem> Values = new();
    public int Count => Values.Count;
    public void LockItemsForRead(bool value)
    {
    }
}
class ScenePresence
{
    public bool IsViewerUIGod;
}
interface IClientAPI
{
    UUID AgentId { get; }
    object SceneAgent { get; }
    void SendTaskInventory(UUID part, short serial, byte[] filename);
}
interface IXfer
{
    bool AddNewFile(string name, byte[] data);
}
class Permissions
{
    public bool Edit;
    public Func<UUID, UUID, bool> Callback;
    public bool CanEditObjectInventory(UUID part, UUID agent) => Callback(part, agent);
}
class Scene
{
    public Permissions Permissions = new();
}
class Group
{
    public Scene Scene = new();
}
class Part
{
    public UUID UUID;
    public Group ParentGroup = new();
}
class Client : IClientAPI
{
    public UUID AgentId => TaskFileReference.Id(9);
    public object SceneAgent { get; set; }
    public Action<UUID, short, byte[]> Reply;
    public void SendTaskInventory(UUID part, short serial, byte[] filename) => Reply(part, serial, filename);
}
class Xfer : IXfer
{
    public Func<string, byte[], bool> Add;
    public bool AddNewFile(string name, byte[] data) => Add(name, data);
}
class Util
{
    public static byte[] StringToBytes256(ReadOnlySpan<char> value) => Utils.StringToBytes(value, 255);
}
partial class TaskFileReference
{
    byte[] m_inventoryFileData = Array.Empty<byte>(), m_inventoryFileNameBytes = Array.Empty<byte>();
    string m_inventoryFileName = "";
    uint m_inventoryFileNameSerial, m_inventorySerial;
    int m_inventoryPrivileged;
    object m_inventoryFileLock = new();
    Items m_items = new();
    Part m_part = new() { UUID = Id(1) };
    List<string> Events = new();
    int Step, Random, Scenario;
    public static UUID Id(int n) => new UUID("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    UUID NextUuid()
    {
        var value = Id(800 + (++Random));
        Events.Add("G:" + value);
        if ((Scenario & 32) != 0 && Step == 0)
            throw new Exception();
        return value;
    }
    static TaskInventoryItem Item(int type, int inv, uint permissions, int scenario)
    {
        var item = new TaskInventoryItem { ItemID = Id(2),
                                           AssetID = Id(3),
                                           CreatorID = Id(4),
                                           LastOwnerID = Id(5),
                                           OwnerID = (scenario & 1) != 0 ? Id(7) : Id(6),
                                           GroupID = Id(7),
                                           CreationDate = uint.MaxValue,
                                           Flags = 0xabcdef01,
                                           BasePermissions = 0xfedcba98,
                                           CurrentPermissions = permissions,
                                           GroupPermissions = 0x1234,
                                           EveryonePermissions = 0x80000000,
                                           NextPermissions = 0x7fffffff,
                                           Type = type,
                                           InvType = inv,
                                           Name = "Script",
                                           Description = "Description" };
        if ((scenario & 2) != 0)
        {
            item.Name = "名|🙂\n";
            item.Description = "雪\t<&>";
        }
        if ((scenario & 4) != 0)
            item.Name = "a\0b";
        return item;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int type = int.Parse(p[0]), inv = int.Parse(p[1]), scenario = int.Parse(p[3]);
            uint permissions = uint.Parse(p[2]);
            var host = new TaskFileReference { Scenario = scenario };
            var client = new Client();
            var xfer = new Xfer();
            bool edit = true;
            host.m_part.ParentGroup.Scene.Permissions.Callback = (part, agent) =>
            {
                host.Events.Add("P:" + part + ":" + agent);
                if ((scenario & 16) != 0 && host.Step == 2)
                    throw new Exception();
                return edit;
            };
            client.Reply = (part, serial, name) =>
            {
                host.Events.Add("R:" + part + ":" + serial + ":" +
                                Convert.ToHexString(name).ToLowerInvariant());
                if ((scenario & 64) != 0 && host.Step == 0)
                    throw new Exception();
            };
            xfer.Add = (name, data) =>
            {
                host.Events.Add("F:" + name + ":" + Convert.ToHexString(data).ToLowerInvariant());
                if ((scenario & 8) != 0 && host.Step == 0)
                    throw new Exception();
                return (scenario & 128) == 0;
            };
            string status = "";
            for (host.Step = 0; host.Step < 11; host.Step++)
            {
                int step = host.Step;
                host.m_inventorySerial = step == 4   ? 0u
                                         : step < 5  ? 1u
                                         : step == 5 ? 65535u
                                         : step < 9  ? 65536u
                                                     : 65537u;
                edit = step != 2;
                client.SceneAgent = step == 3 ? new ScenePresence { IsViewerUIGod = true } : null;
                host.m_items.Values.Clear();
                if (step != 7)
                    host.m_items.Values.Add(Item(step >= 9 ? -1 : type, inv, permissions, scenario));
                try
                {
                    host.RequestInventoryFile(client, xfer);
                    status += 'O';
                }
                catch (IndexOutOfRangeException)
                {
                    status += 'B';
                }
                catch (Exception)
                {
                    status += 'E';
                }
            }
            Console.WriteLine(status + "|" + string.Join('|', host.Events));
        }
    }
}

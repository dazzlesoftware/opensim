using System;
using System.Collections.Generic;
using System.Text;
using OpenMetaverse;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
namespace log4net
{
    public interface ILog
    {
        void Debug(object o);
        void Warn(object o);
        void ErrorFormat(string f, params object[] a);
    }
    public static class LogManager
    {
        class Log : ILog
        {
            public void Debug(object o)
            {
            }
            public void Warn(object o)
            {
            }
            public void ErrorFormat(string f, params object[] a)
            {
            }
        }
        public static ILog GetLogger(Type t) => new Log();
    }
}
namespace OpenSim.Framework
{
    public class osUTF8
    {
        string value;
        public osUTF8(string s)
        {
            value = s;
        }
        public override string ToString() => value;
    }
}
public class Logger
{
    public void DebugFormat(string f, params object[] args)
    {
    }
    public void WarnFormat(string f, params object[] args)
    {
    }
    public void ErrorFormat(string f, params object[] args)
    {
    }
}
public class InventoryFixture
{
    public Func<UUID, FolderType, InventoryFolderBase> folder;
    public Func<InventoryItemBase, bool> add;
    public Func<UUID, InventoryFolderBase> root;
    public Func<InventoryFolderBase, bool> addfolder;
    public Func<UUID, UUID, InventoryItemBase> get;
    public InventoryFolderBase GetFolderForType(UUID principal, FolderType type) => folder(principal, type);
    public bool AddItem(InventoryItemBase item) => add(item);
    public InventoryFolderBase GetRootFolder(UUID principal) => root(principal);
    public bool AddFolder(InventoryFolderBase folder) => addfolder(folder);
    public InventoryItemBase GetItem(UUID principal, UUID item) => get(principal, item);
}
public class AvatarFixture
{
    public Func<UUID, AvatarAppearance, bool> set;
    public Func<UUID, AvatarAppearance> get;
    public bool SetAppearance(UUID principal, AvatarAppearance a) => set(principal, a);
    public AvatarAppearance GetAppearance(UUID principal) => get(principal);
}
public static class Fixture
{
    public static ProvisionerReference current;
    public static int Clock() => current.NextClock();
}
public partial class ProvisionerReference
{
    InventoryFixture m_InventoryService;
    AvatarFixture m_AvatarService;
    Logger m_log = new Logger();
    string trace = "";
    int outcome, target, mutation, step, random, clock, adds;
    List<InventoryItemBase> items = new();
    Dictionary<int, InventoryFolderBase> folders = new();
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    bool Touch()
    {
        ++step;
        if (outcome == 2 && step == target)
            throw new Exception();
        return !(outcome == 1 && step == target);
    }
    UserAccount GetUserAccount(UUID scope, string first, string last)
    {
        trace += "lookup:" + scope + ":" + first + ":" + last + ";";
        bool ok = Touch();
        return ok && mutation != 0 ? new UserAccount(Id(8)) : null;
    }
    void CopyWearablesAndAttachments(UUID destination, UUID source, AvatarAppearance a)
    {
        trace += "copy:" + destination + ":" + source + ";";
        Touch();
        if (mutation == 3)
            throw new Exception();
        a.SetWearable(0, new AvatarWearable(Id(555), Id(556)));
    }
    UUID NextId()
    {
        ++random;
        trace += "random:" + random + ";";
        if (outcome == 3 && random == target)
            throw new Exception();
        BeforeRandom();
        if (mutation == 4 && random == 2 && items.Count > 0)
        {
            items[0].ID = Id(905);
            items[0].Name = "RandomChanged";
        }
        return Id(1000 + random);
    }
    public int NextClock()
    {
        if (buildingFixture)
            return 0;
        ++clock;
        trace += "clock:" + clock + ";";
        if (outcome == 4 && clock == target)
            throw new Exception();
        BeforeClock();
        return 100 + clock;
    }
    static string Item(InventoryItemBase a) => a.ID + "," + a.Owner + "," + a.AssetID + "," + a.AssetType +
                                               "," + a.Name + "," + a.Description + "," + a.InvType +
                                               "," + a.CreatorIdentification + "," + a.NextPermissions +
                                               "," + a.CurrentPermissions + "," + a.BasePermissions +
                                               "," + a.EveryOnePermissions + "," + a.GroupPermissions +
                                               "," + a.GroupID + "," + (a.GroupOwned ? 1 : 0) +
                                               "," + a.SalePrice + "," + a.SaleType + "," + a.Flags +
                                               "," + a.CreationDate + "," + a.Folder;
    static string Appearance(AvatarAppearance a)
    {
        string s = a.Serial + ":";
        foreach (byte b in a.VisualParams)
            s += b + ",";
        s += ':';
        foreach (byte b in a.Texture.GetBytes())
            s += b + ",";
        s += ':';
        if (a.Wearables == null)
            s += "null";
        else
            foreach (var w in a.Wearables)
            {
                if (w == null)
                {
                    s += "null;";
                    continue;
                }
                s += '[';
                for (int j = 0; j < w.Count; ++j)
                {
                    var n = w[j];
                    s += n.ItemID + "," + n.AssetID + ";";
                }
                s += ']';
            }
        s += ':';
        foreach (var attachment in a.GetAttachments())
            s += attachment.AttachPoint + "," + attachment.ItemID + "," + attachment.AssetID + ";";
        return s;
    }
    public static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int avatar = int.Parse(p[0]), link = int.Parse(p[4]);
            var owner = new ProvisionerReference { outcome = int.Parse(p[1]), target = int.Parse(p[2]),
                                                   mutation = int.Parse(p[3]) };
            Fixture.current = owner;
            foreach (int type in new[] { 13, 46, 5 })
                owner.folders[type] = new InventoryFolderBase { ID = Id(100 + type) };
            owner.m_InventoryService =
                new InventoryFixture { folder =
                                           (principal, type) =>
                                       {
                                           owner.trace += "folder:" + principal + ":" + (int)type + ";";
                                           if (!owner.Touch())
                                               return null;
                                           return owner.folders[(int)type];
                                       },
                                       add =
                                           item =>
                                       {
                                           owner.trace += "add:" + Item(item) + ";";
                                           bool result = owner.Touch();
                                           owner.items.Add(item);
                                           ++owner.adds;
                                           if (owner.mutation == 1 && owner.adds == 1)
                                           {
                                               item.ID = Id(900);
                                               item.AssetID = Id(901);
                                               item.Name = "Changed";
                                               item.Flags = 99;
                                           }
                                           if (owner.mutation == 2 && owner.adds == 1)
                                               owner.folders[46].ID = Id(902);
                                           if (owner.mutation == 3 && owner.adds == 11)
                                           {
                                               owner.items[0].ID = Id(903);
                                               owner.items[0].AssetID = Id(904);
                                               owner.items[0].Name = "LateChanged";
                                           }
                                           return result;
                                       } };
            if (avatar != 0)
                owner.m_AvatarService =
                    new AvatarFixture { set =
                                            (principal, a) =>
                                        {
                                            owner.trace +=
                                                "appearance:" + principal + ":" + Appearance(a) + ";";
                                            return owner.Touch();
                                        },
                                        get =
                                            principal =>
                                        {
                                            owner.trace += "get:" + principal + ";";
                                            bool ok = owner.Touch();
                                            return ok && owner.mutation != 1 ? new AvatarAppearance() : null;
                                        } };
            string[] models = { "First Last", " First Last", "First  Last",    "First",
                                "",           " ",           "First\tLast",    "First\u00a0Last",
                                "試験 😀",    "First\nLast", "First\u200bLast" };
            if (link >= 100)
                owner.SetupTransfer(avatar, link - 100);
            string status = "OK";
            try
            {
                if (link >= 100)
                    owner.OriginalCopyWearablesAndAttachments(Id(7), Id(8), owner.transfer);
                else if (link >= 3)
                    owner.EstablishAppearance(Id(7), models[link - 3]);
                else if (link != 0)
                    owner.CreateCurrentOutfitLink(link == 1 ? 18 : -1, link == 1 ? 5u : 0xffffffffu,
                                                  "Link 試験 <&>", Id(7), Id(8), Id(9));
                else
                    owner.CreateDefaultAppearanceEntries(Id(7));
            }
            catch
            {
                status = "EX";
            }
            string final = "";
            foreach (var item in owner.items)
                final += Item(item) + ";";
            string result = status + "|" +
                            Convert.ToHexString(Encoding.UTF8.GetBytes(owner.trace)).ToLowerInvariant() +
                            "|" + Convert.ToHexString(Encoding.UTF8.GetBytes(final)).ToLowerInvariant();
            if (link >= 100)
                result += "|" + Convert
                                    .ToHexString(Encoding.UTF8.GetBytes(
                                        owner.transfer == null ? "null" : Appearance(owner.transfer)))
                                    .ToLowerInvariant();
            Console.WriteLine(result);
        }
    }
}

using System;
using System.Collections.Generic;
using OpenMetaverse;
using OpenSim.Framework;
public partial class ProvisionerReference
{
    AvatarAppearance transfer;
    Dictionary<UUID, InventoryItemBase> sourceItems = new();
    AvatarAttachment second;
    int transferGets;
    bool buildingFixture;
    void SetupTransfer(int wrongFolder, int variant)
    {
        buildingFixture = true;
        folders[5].Type = (short)(wrongFolder != 0 ? 6 : 5);
        folders[46].Type = 46;
        transfer = new AvatarAppearance { Wearables = new AvatarWearable[] { new AvatarWearable(), null,
                                                                             new AvatarWearable() } };
        var w = transfer.Wearables[0];
        for (int n = 11; n <= 15; ++n)
            w.Add(Id(n), Id(190 + n));
        void Make(int n, int type, int asset)
        {
            sourceItems[Id(n)] = new InventoryItemBase(Id(n), Id(8)) { AssetID = Id(asset),
                                                                       AssetType = type,
                                                                       InvType = type == 6 ? 6 : 18,
                                                                       CreatorId = "creator",
                                                                       CreatorData = "https://profile;Name",
                                                                       Name = "Source 試験 " + n,
                                                                       Description = "Description",
                                                                       NextPermissions = 0xa000,
                                                                       CurrentPermissions = 0xffffffff,
                                                                       BasePermissions = 0xf0f0f0f0,
                                                                       EveryOnePermissions = 0xfffffff,
                                                                       GroupPermissions = 0xffff0000,
                                                                       GroupID = Id(44),
                                                                       GroupOwned = true,
                                                                       SalePrice = 123,
                                                                       SaleType = 200,
                                                                       CreationDate = -123,
                                                                       Flags = 0x80000003 };
        }
        Make(11, 13, 201);
        Make(12, 24, 22);
        Make(13, 24, 0);
        Make(15, 24, 33);
        Make(22, 13, 222);
        Make(33, 24, 34);
        Make(21, 6, 301);
        transfer.AppendAttachment(new AvatarAttachment(1, Id(21), Id(301)));
        second = new AvatarAttachment(2, Id(22), Id(302));
        transfer.AppendAttachment(second);
        transfer.AppendAttachment(new AvatarAttachment(3, UUID.Zero, Id(303)));
        if (variant == 1)
            transfer.Wearables = Array.Empty<AvatarWearable>();
        if (variant == 2)
            transfer.Wearables = null;
        if (variant == 3)
            transfer = null;
        if (variant == 4)
            transfer.Wearables[0].Clear();
        if (variant == 5)
        {
            transfer.ClearAttachments();
            sourceItems.Clear();
        }
        if (variant == 6)
        {
            transfer.ClearAttachments();
            w.Clear();
            w.Add(Id(15), Id(205));
        }
        m_InventoryService.root = principal =>
        {
            trace += "root:" + principal + ";";
            return Touch() ? new InventoryFolderBase { ID = Id(88) } : null;
        };
        m_InventoryService.addfolder = folder =>
        {
            trace += "addfolder:" + folder.ID + ":" + folder.Owner + ":" + folder.Name + ":" + folder.Type +
                     ":" + folder.ParentID + ":" + folder.Version + ";";
            bool result = Touch();
            if (mutation == 9)
                folder.ID = Id(944);
            return result;
        };
        m_InventoryService.get = (principal, item) =>
        {
            trace += "item:" + principal + ":" + item + ";";
            bool ok = Touch();
            ++transferGets;
            if (mutation == 6 && transferGets == 1)
                transfer.SetWearable(20, new AvatarWearable(Id(99), Id(199)));
            if (mutation == 7 && transferGets == 1 && transfer.Wearables != null &&
                transfer.Wearables.Length > 0 && transfer.Wearables[0] != null)
                transfer.Wearables[0].RemoveItem(Id(12));
            if (mutation == 5 && item == Id(21))
            {
                second.AttachPoint = 55;
                second.ItemID = Id(11);
            }
            if (mutation == 8 && item == Id(21))
                transfer.AppendAttachment(new AvatarAttachment(70, Id(31), Id(888)));
            return ok && sourceItems.TryGetValue(item, out var value) ? value : null;
        };
        buildingFixture = false;
    }
    void BeforeRandom()
    {
        if (mutation == 11 && random == 1 && sourceItems.Count > 0)
        {
            var a = sourceItems[Id(11)];
            a.Name = "Random source";
            a.AssetID = Id(777);
            a.NextPermissions = 32768;
        }
    }
    void BeforeClock()
    {
        if (mutation == 12 && clock == 1 && sourceItems.Count > 0)
        {
            var a = sourceItems[Id(11)];
            a.Name = "Clock source";
            a.CreationDate = -999;
        }
    }
}

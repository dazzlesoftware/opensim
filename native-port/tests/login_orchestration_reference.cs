// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Net;
using OpenMetaverse;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
using OpenSim.Services.GridService;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
namespace OpenSim.Services.Interfaces
{
    public class UserAccount
    {
        public UUID PrincipalID;
        public int UserLevel;
        public string FirstName = "First", LastName = "Last";
        public string Name => FirstName + " " + LastName;
    }
}
namespace OpenSim.Framework
{
    public class AvatarAppearance
    {
    }
}
public class InventoryFolderBase
{
}
public static class LLFailedLoginResponse
{
    public const string AlreadyLoggedInProblem = "AlreadyLoggedInProblem",
                        InventoryProblem = "InventoryProblem", GridProblem = "GridProblem";
}
public partial class DestinationReference
{
    public static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    public int Seed;
    public string Events = "";
    public bool Active;
    public GridUserInfo User;
    Avatar m_AvatarService;
    public class Avatar
    {
        public DestinationReference Parent;
        public AvatarAppearance GetAppearance(UUID id)
        {
            Parent.Events += "appearance:" + id + ";";
            if ((Parent.Seed & 256) != 0)
                throw new Exception();
            return (Parent.Seed & 64) != 0 ? null : new AvatarAppearance();
        }
    }
    bool m_allowDuplicatePresences, m_RequireInventory;
    UserService m_GridUserService;
    Inventory m_InventoryService, m_HGInventoryService;
    Presence m_PresenceService;
    public class UserService
    {
        public DestinationReference Parent;
        public GridUserInfo GetGridUserInfo(string user)
        {
            Parent.Events += "grid-user;";
            return Parent.User;
        }
        public bool LoggedOut(string user, UUID session, UUID region, Vector3 position, Vector3 look)
        {
            Parent.Events += "logout:" + session + ";";
            if (Parent.User != null)
                Parent.User.Online = false;
            return true;
        }
    }
    public class Inventory
    {
        public DestinationReference Parent;
        public void GetRootFolder(UUID user)
        {
            Parent.Events += "suitcase;";
            if (Parent.Seed == 7)
                throw new Exception();
        }
        public List<InventoryFolderBase> GetInventorySkeleton(UUID user)
        {
            Parent.Events += "inventory;";
            int kind = Parent.Seed % 6;
            if (kind == 4)
                throw new Exception();
            if (kind == 1)
                return null;
            var list = new List<InventoryFolderBase>();
            for (int i = 0; i < (kind == 2 ? 0 : kind == 3 ? 1 : 2); i++)
                list.Add(new());
            return list;
        }
    }
    public class Presence
    {
        public DestinationReference Parent;
        public bool LoginAgent(string user, UUID session, UUID secure)
        {
            Parent.Events += "presence-login;";
            Parent.Active = true;
            if (Parent.Seed % 5 == 2)
                throw new Exception();
            return Parent.Seed % 5 != 1;
        }
        public bool LogoutAgent(UUID session)
        {
            Parent.Events += "presence-logout;";
            if (Parent.Seed == 59 || Parent.Seed == 63)
                throw new Exception();
            Parent.Active = false;
            return true;
        }
    }
    bool SendAgentGodKillToRegion(UUID scope, UUID user, GridUserInfo info)
    {
        Events += "recover;";
        if (Seed % 3 == 2)
            throw new Exception();
        if (Seed % 3 == 1)
            m_GridUserService.LoggedOut(user.ToString(), UUID.Zero, info.LastRegionID, info.LastPosition,
                                        info.LastLookAt);
        return Seed % 3 == 1;
    }
    public void Configure(int seed)
    {
        if ((seed & 128) != 0)
            m_AvatarService = new Avatar { Parent = this };
        Seed = seed;
        m_allowDuplicatePresences = (seed & 16) != 0;
        m_RequireInventory = (seed & 1) == 0;
        m_GridUserService = new UserService { Parent = this };
        if (seed % 6 != 0)
            m_InventoryService = new Inventory { Parent = this };
        if ((seed & 2) != 0)
            m_HGInventoryService = new Inventory { Parent = this };
        if (seed % 5 != 0)
            m_PresenceService = new Presence { Parent = this };
        if (seed % 4 != 0)
            User = new GridUserInfo { HomeRegionID = Id(1), LastRegionID = seed % 4 == 3 ? UUID.Zero : Id(1),
                                      Online = seed % 4 >= 2, HomePosition = new(128, 128, 30),
                                      LastPosition = new(1, 2, 3) };
    }
    // The generated partial class contains the verbatim post-authentication block.
}
public static class PreparationProgram
{
    public static void Main(string[] args)
    {
        Culture.SetCurrentCulture();
        int seed = int.Parse(args[0]);
        var config = new Nini.Config.IniConfigSource();
        config.AddConfig("GridService");
        var store = new FaultRegionStore();
        var grid = new GridService(store, config, null);
        var region = new GridRegion { RegionID = DestinationReference.Id(1),
                                      RegionName = "Region",
                                      RegionLocX = 25600,
                                      RegionLocY = 25600,
                                      RegionSizeX = 256,
                                      RegionSizeY = 256,
                                      InternalEndPoint = new IPEndPoint(IPAddress.Any, 9000),
                                      ExternalHostName = "127.0.0.1" };
        var row = grid.RegionInfo2RegionData(region);
        row.Data["flags"] = "5";
        store.Store(row);
        var target = new DestinationReference(seed % 7 == 0 ? null : grid, (seed & 32) == 0);
        target.Configure(seed);
        string start = seed % 4 == 0   ? "home"
                       : seed % 4 == 1 ? "last"
                       : seed % 4 == 2 ? "uri:Region&1&2&3"
                                       : "invalid";
        string result;
        try
        {
            result = target.Prepare(new UserAccount { PrincipalID = DestinationReference.Id(9),
                                                      UserLevel = (seed & 8) != 0 ? 200 : 0 },
                                    start);
        }
        catch
        {
            result = "THREW";
        }
        Console.WriteLine(result + "|" + target.Events + "|" + (target.Active ? 1 : 0) + "|" +
                          (target.User != null && target.User.Online ? 1 : 0));
    }
}

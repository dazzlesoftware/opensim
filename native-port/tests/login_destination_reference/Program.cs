// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Linq;
using System.Net;
using OpenMetaverse;
using OpenSim.Services.Interfaces;
using OpenSim.Services.GridService;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
namespace OpenSim.Services.Interfaces
{
    public class UserAccount
    {
        public string FirstName = "First", LastName = "Last";
        public string Name => FirstName + " " + LastName;
    }
}
static class DestinationProgram
{
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    static string Bits(Vector3 v) => string.Join(
        ",", new[] { v.X, v.Y, v.Z }.Select(x => BitConverter.SingleToInt32Bits(x).ToString("x8")));
    static void Main(string[] args)
    {
        OpenSim.Framework.Culture.SetCurrentCulture();
        int seed = int.Parse(args[0]);
        var config = new Nini.Config.IniConfigSource();
        config.AddConfig("GridService");
        var store = new FaultRegionStore();
        var grid = new GridService(store, config, null);
        for (int i = 1; i <= 3; i++)
        {
            var region = new GridRegion { RegionID = Id(i),
                                          RegionName = i == 1   ? "Alpha"
                                                       : i == 2 ? "Default"
                                                                : "Fallback",
                                          RegionLocX = i == 1   ? 25600
                                                       : i == 2 ? 256000
                                                                : 255744,
                                          RegionLocY = i == 1 ? 25600 : 256000,
                                          RegionSizeX = i == 1 ? (seed % 5 == 0 ? 0 : 512) : 256,
                                          RegionSizeY = i == 1 ? (seed % 7 == 0 ? 0 : 256) : 256,
                                          InternalEndPoint = new IPEndPoint(IPAddress.Any, 9000),
                                          ExternalHostName = "127.0.0.1" };
            var row = grid.RegionInfo2RegionData(region);
            row.Data["flags"] = (i == 1   ? 4
                                 : i == 2 ? ((seed & 1) != 0 ? 5 : 0)
                                          : ((seed & 2) != 0 ? 2 : 0) | ((seed & 4) != 0 ? 4 : 0) |
                                                ((seed & 8) != 0 ? 512 : 0))
                                    .ToString();
            store.Store(row);
        }
        var selector = new DestinationReference(grid, (seed & 16) == 0);
        var info = new GridUserInfo { HomeRegionID = Id(1), LastRegionID = Id(1),
                                      HomePosition = new(-50, 9999, 5.5f), HomeLookAt = new(-1, 0, 1),
                                      LastLookAt = new(1, -1, 0) };
        info.LastPosition = (seed % 4) switch {
            0 => new(-100, 10000, -25),
            1 => new(float.NaN, float.NaN, float.PositiveInfinity),
            2 => new(float.PositiveInfinity, float.NegativeInfinity, 70),
            _ => new(128.25f, .25f, -0f)
        };
        var home = grid.GetRegionByUUID(UUID.Zero, Id(1));
        void Observe(DestinationReference target, string start, GridUserInfo user, GridRegion homeRegion)
        {
            try
            {var region=target.Find(UUID.Zero,user,start,homeRegion,out var gate,out var where,out var position,out var look,out var flags);Console.WriteLine((region?.RegionID.ToString()??"-")+"|"+(gate?.RegionID.ToString()??"-")+"|"+where+"|"+Bits(position)+"|"+Bits(look)+"|"+(uint)flags);
            }
            catch
            {
                Console.WriteLine("EX");
            }
        }
        if (args.Length > 1 && args[1] == "--stdin")
        {
            string line;
            while ((line = Console.ReadLine()) != null)
                Observe(selector, System.Text.Encoding.UTF8.GetString(Convert.FromHexString(line)), info,
                        null);
            return;
        }
        Observe(selector, "home", info, home);
        Observe(selector, "home", info, null);
        Observe(selector, "home", null, home);
        Observe(selector, "last", info, null);
        info.LastRegionID = UUID.Zero;
        Observe(selector, "last", info, null);
        info.LastRegionID = Id(99);
        Observe(selector, "last", info, null);
        Observe(selector, "last", null, null);
        foreach (var start in new[] {
                     "uri:Alpha&1.25&2.&300", "uri:Missing&90&91&92", "uri:Alpha@remote.example&1&2&3",
                     "uri:http://local.example/Alpha&1&2&3", "", "home ", "uri:Alpha&-1&2&3",
                     "uri:Alpha&1&2&-3", "uri:Alpha&1e2&2&3", "uri:Alpha&" + new string('9', 90) + "&2&3",
                     "uri:Alpha&0." + new string('0', 400) + "1&2&3", "uri:Alpha&1&2&3\n", "Alpha&1&2&3",
                     null, "uri:&1&2&3", "uri:Alpha&\u0661&2&3", "uri:http://remote.example/Alpha&1&2&3"
                 })
            Observe(selector, start, info, null);
        Observe(new DestinationReference(null, true), null, info, home);
    }
}

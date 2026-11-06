/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
using System;
using System.Net;
using System.Linq;
using System.Text;
using System.Collections.Generic;
using OpenMetaverse;
using OpenSim.Data;
using OpenSim.Data.Null;
using OpenSim.Services.Interfaces;
using GridRegion = OpenSim.Services.Interfaces.GridRegion;
using OpenSim.Services.GridService;
static class Program
{
    static string Hex(string value) => Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    static void Emit(Func<string> value)
    {
        try
        {
            Console.WriteLine(Hex(value()));
        }
        catch
        {
            Console.WriteLine(Hex("EX"));
        }
    }
    static string Info(GridRegion region)
    {
        if (region == null)
            return "-";
        return string.Join(
                   ";", region.ToKeyValuePairs().Select(
                            pair => pair.Key + "=" + (pair.Value == null ? "NULL" : pair.Value.ToString()))) +
               "|" + region.ScopeID + "|" + region.RegionCoordX + "|" + region.RegionCoordY + "|" +
               region.RegionHandle + "|" + region.Maturity + "|" +
               (region.RawServerURI == null ? "NULL" : region.RawServerURI);
    }
    static string Rows(List<GridRegion> rows) => string.Join(",",
                                                             rows.Select(row => row.RegionID.ToString()));
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    class Auth : IAuthenticationService
    {
        readonly bool allowed;
        public Auth(bool value)
        {
            allowed = value;
        }
        public bool Verify(UUID id, string token,
                           int time) => allowed && id == Id(9) && token == "token" && time == 30;
    }
    static GridRegion Region(int id, string name, int x, int y, int sx = 256,
                             int sy = 256) => new() { RegionID = Id(id),
                                                      RegionName = name,
                                                      RegionLocX = x,
                                                      RegionLocY = y,
                                                      RegionSizeX = sx,
                                                      RegionSizeY = sy,
                                                      InternalEndPoint = new IPEndPoint(IPAddress.Any, 9000),
                                                      ExternalHostName = "127.0.0.1",
                                                      HttpPort = 9001,
                                                      Token = "token",
                                                      ScopeID = Id(8) };
    static void Main(string[] args)
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        if (args[0] == "--model")
        {
            string line;
            while ((line = Console.ReadLine()) != null)
            {
                Emit(() =>
                     {
                         var values = new Dictionary<string, object>();
                         foreach (var field in line.Split('|', StringSplitOptions.RemoveEmptyEntries))
                         {
                             int split = field.IndexOf('=');
                             var name =
                                 Encoding.UTF8.GetString(Convert.FromHexString(field.Substring(0, split)));
                             var value = field.Substring(split + 1);
                             values[name] =
                                 value == "N" ? null : Encoding.UTF8.GetString(Convert.FromHexString(value));
                         }
                         return Info(new GridRegion(values));
                     });
            }
            return;
        }
        int seed = int.Parse(args[0]);
        var config = new Nini.Config.IniConfigSource();
        var section = config.AddConfig("GridService");
        section.Set("RegionsAutoRegistration", (seed & 8) == 0);
        section.Set("AllowDuplicateNames", (seed & 1) != 0);
        section.Set("AllowHypergridMapSearch", (seed & 1) != 0);
        section.Set("DeleteOnUnregister", (seed & 16) == 0);
        section.Set("DefaultRegionFlags", "+DefaultRegion +FallbackRegion");
        if ((seed & 1) == 0)
            section.Set("Region_Alpha", "-FallbackRegion +DefaultHGRegion");
        var store = new FaultRegionStore();
        var grid = new GridService(store, config, (seed & 4) != 0 ? new Auth((seed & 2) != 0) : null);
        var first = Region(1, "Alpha", 25600, 25600, 256 * (1 + seed % 3), 256 * (1 + (seed / 3) % 2));
        var second = Region(2, "Alpha", 30000, 30000);
        var other = Region(3, "Beta", first.RegionLocX + first.RegionSizeX, first.RegionLocY);
        Emit(() => grid.RegisterRegion(UUID.Zero, Region(0, "Zero", 25600, 25600)));
        Emit(() => grid.RegisterRegion(UUID.Zero, Region(4, "", 25600, 25600)));
        Emit(() => grid.RegisterRegion(UUID.Zero, Region(4, "Reserved", 25600, 4096)));
        if ((seed & 8) != 0)
        {
            var initial = grid.RegionInfo2RegionData(first);
            initial.Data["flags"] = "0";
            store.Store(initial);
        }
        Emit(() => grid.RegisterRegion(Id(8), first));
        Emit(() => Info(grid.GetRegionByUUID(Id(100), Id(1))));
        Emit(() => grid.RegisterRegion(Id(8), second));
        Emit(() => grid.RegisterRegion(Id(8), other));
        Emit(() => grid.RegisterRegion(Id(8), Region(4, "Overlap", 25600, 25600)));
        Emit(() => Info(grid.GetRegionByPosition(Id(8), 25601, 25601)));
        Emit(() => Info(grid.GetRegionByHandle(Id(8), first.RegionHandle)));
        Emit(() => Rows(grid.GetNeighbours(Id(8), Id(1))));
        Emit(() => Rows(grid.GetRegionRange(Id(8), 25599, 31001, 25599, 31001)));
        Emit(() => Rows(grid.GetDefaultRegions(Id(8))));
        Emit(() => Rows(grid.GetDefaultHypergridRegions(Id(8))));
        Emit(() => Rows(grid.GetFallbackRegions(Id(8), 25700, 25700)));
        Emit(() => Rows(grid.GetOnlineRegions(Id(8), 25700, 25700, 0)));
        Emit(() => Rows(grid.GetOnlineRegions(Id(8), 25700, 25700, 2)));
        Emit(() => Rows(grid.GetHyperlinks(Id(8))));
        int[] flags = { 16, 64, 32, 128, 256, 512 | 4, 1024 | 1 | 4, 0 };
        var row = store.Get(Id(1), UUID.Zero);
        row.Data["flags"] = flags[seed % 8].ToString();
        row.Data["PrincipalID"] = (seed % 4 == 0 ? UUID.Zero : Id(9)).ToString();
        Emit(() => grid.RegisterRegion(Id(8), first));
        Emit(() => grid.GetRegionFlags(Id(8), Id(1)).ToString());
        first.RegionLocX += 8192;
        Emit(() => grid.RegisterRegion(Id(8), first));
        Emit(() => Info(grid.GetRegionByUUID(Id(8), Id(1))));
        Emit(() => grid.DeregisterRegion(Id(1)) ? "1" : "0");
        Emit(() => Info(grid.GetRegionByUUID(Id(8), Id(1))));
        Emit(() => grid.GetRegionFlags(Id(8), Id(99)).ToString());
        Emit(() => grid.Flags(3, "DefaultRegion -fallbackregion +PERSISTENT -DefaultRegion unknown +1024"));
        Emit(() => grid.Flags(-1, "-2147483648 +0 -1"));
        // Provider quirks: scope is ignored; deletion reuses dictionary slots;
        // range Y extent uses sizeX, and !static flags still query the singleton.
        var tall = Region(10, "Tall", 1000, 10000, 256, 1024);
        var tallrow = grid.RegionInfo2RegionData(tall);
        tallrow.Data["flags"] = "5";
        store.Store(tallrow);
        Emit(() => Rows(
                 store.Get(1000, 10500, 1001, 10501, Id(999)).Select(grid.RegionData2RegionInfo).ToList()));
        Emit(() => Info(grid.RegionData2RegionInfo(store.Get(1000, 10500, Id(999)))));
        store.Delete(Id(2));
        var replacement = grid.RegionInfo2RegionData(Region(11, "Replacement", 50000, 50000));
        replacement.Data["flags"] = "5";
        store.Store(replacement);
        Emit(() => string.Join(",", store.Get("%", UUID.Zero).Select(value => value.RegionID.ToString())));
        Emit(() => store.Get("missing", UUID.Zero) == null ? "null" : "list");
        var isolated = new NullRegionData("!static", "");
        Emit(() => string.Join(
                 ",", isolated.GetDefaultRegions(UUID.Zero).Select(value => value.RegionID.ToString())));
        Emit(() => isolated.Get(Id(10), UUID.Zero) == null ? "missing" : "found");
        var copy = new GridRegion(first);
        Emit(() => Info(copy));
        Emit(() => first.Equals(first) ? "1" : "0");
        Emit(() => new GridRegion().Equals(new GridRegion()) ? "1" : "0");
        var failed = Region(12, "Failed", 60000, 60000);
        store.StoreFalse = true;
        Emit(() => grid.RegisterRegion(Id(8), failed));
        Emit(() => Info(grid.GetRegionByUUID(Id(8), Id(12))));
        store.StoreFalse = false;
        store.StoreThrows = true;
        Emit(() => grid.RegisterRegion(Id(8), failed));
        store.StoreThrows = false;
        var moving = Region(13, "Moving", 70000, 70000);
        var movingrow = grid.RegionInfo2RegionData(moving);
        movingrow.Data["flags"] = "4";
        store.Store(movingrow);
        moving.RegionLocX += 8192;
        store.EraseThrows = true;
        Emit(() => grid.RegisterRegion(Id(8), moving));
        store.EraseThrows = false;
        store.EraseFalse = true;
        Emit(() => grid.RegisterRegion(Id(8), moving));
        store.EraseFalse = false;
        Emit(() => Info(grid.GetRegionByUUID(Id(8), Id(13))));
        store.SetDataItem(Id(13), "flags", "20");
        store.StoreThrows = true;
        Emit(() => grid.DeregisterRegion(Id(13)) ? "1" : "0");
        store.StoreThrows = false;
        Emit(() => Info(grid.GetRegionByUUID(Id(8), Id(13))));

        foreach (var extra in new[] { Region(20, "Alpha Town", 80000, 80000),
                                      Region(21, "Other Alpha", 82000, 80000),
                                      Region(22, "Alpha", 84000, 80000),
                                      Region(23, "http://remote.example:80/ World", 86000, 80000) })
        {
            var data = grid.RegionInfo2RegionData(extra);
            data.Data["flags"] = "5";
            store.Store(data);
        }
        Emit(() => Info(grid.GetRegionByName(UUID.Zero, "Alpha")));
        Emit(() => Info(grid.GetRegionByName(UUID.Zero, ".Bad")));
        Emit(() => Rows(grid.GetRegionsByName(UUID.Zero, "Alpha", 2)));
        Emit(() => Rows(grid.GetRegionsByName(UUID.Zero, "Alpha", 0)));
        Emit(() => Rows(grid.GetRegionsByName(UUID.Zero, "Alpha", -1)));
        Emit(() => Info(grid.GetRegionByName(UUID.Zero, "http://local.example/Alpha")));
        Emit(() => Info(grid.GetLocalRegionByName(UUID.Zero, "http://remote.example/World")));
        Emit(() => Info(grid.GetRegionByName(UUID.Zero, "http://remote.example/World")));
        Emit(() => Info(grid.GetRegionByName(UUID.Zero, "http://remote.example/Missing")));
        Emit(() => Rows(grid.GetRegionsByName(UUID.Zero, "http://remote.example/World", 3)));
        Emit(() => Rows(grid.GetRegionsByName(UUID.Zero, "http://remote.example/Missing", 3)));
        Emit(() => Info(grid.GetRegionByName(UUID.Zero, "http://bad.invalid/Alpha")));
        var uri = new OpenSim.Framework.RegionURI("http://local.example/Alpha");
        Emit(() =>
             {
                 grid.GetRegionByURI(UUID.Zero, uri);
                 return ((int)uri.Flags) + "|" + uri.Host + "|" + uri.IsValid;
             });
        for (int index = 0; index < 20; ++index)
        {
            var data = grid.RegionInfo2RegionData(Region(30 + index, "Tied", 256000, 256000));
            data.Data["flags"] = ((int)(OpenSim.Framework.RegionFlags.FallbackRegion |
                                        OpenSim.Framework.RegionFlags.RegionOnline)).ToString();
            store.Store(data);
        }
        Emit(() => Rows(grid.GetFallbackRegions(UUID.Zero, 256000, 256000)));
        Emit(() => Rows(grid.GetOnlineRegions(UUID.Zero, 256000, 256000, 10)));
        store.Store(grid.RegionInfo2RegionData(Region(50, "\u00c9tage Annex", 90000, 90000)));
        store.Store(grid.RegionInfo2RegionData(Region(51, "\u00c9tage", 91000, 90000)));
        Emit(() => Info(grid.GetRegionByName(UUID.Zero, "\u00e9tage")));
        Emit(() => Rows(grid.GetRegionsByName(UUID.Zero, "\u00e9tage", 2)));
    }
}

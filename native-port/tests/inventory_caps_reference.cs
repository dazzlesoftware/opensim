using System;
using System.Collections.Generic;
using System.Net;
using System.IO;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;

class IOSHttpRequest
{
    public string HttpMethod;
    public Stream InputStream;
    public object Headers;
}

class IOSHttpResponse
{
    public int StatusCode = 200;
    public byte[] RawBuffer = Array.Empty<byte>();
    public void AddHeader(string key, string value)
    {
    }
}

class ControlledTimer : System.IDisposable
{
    public static string Trace = "";
    public ControlledTimer(System.Threading.TimerCallback callback, object state, int due, int period)
    {
        Trace += "A:" + due + ":" + (int)OpenSim.Framework.Util.Clock + ";";
    }

    public bool Change(int due, int period)
    {
        Trace += "R:" + due + ":" + (int)OpenSim.Framework.Util.Clock + ";";
        return true;
    }

    public void Dispose()
    {
        Trace += "D:0:" + (int)OpenSim.Framework.Util.Clock + ";";
    }
}

namespace OpenSim.Framework
{
    public static class Util
    {
        public static double Clock;
        public static double GetTimeStampMS() => Clock;
    }
}

class AuthStub
{
    public bool Authenticate(object headers, Action<string, string> add, out HttpStatusCode status)
    {
        status = HttpStatusCode.OK;
        return true;
    }
}

partial class Wrapper
{
    public int RequestsReceived, RequestsHandled;
    public string m_httMethod = "POST";
    public AuthStub m_Auth;
    public Action<IOSHttpRequest, IOSHttpResponse, OSDMap> m_processRequest;
    protected void ProcessRequest(IOSHttpRequest request, IOSHttpResponse response, OSDMap args)
    {
    }
}

partial class InventoryItemBase
{
    public UUID Folder, AssetID, ID, CreatorIdAsUuid, Owner, GroupID;
    public uint CurrentPermissions, GroupPermissions, EveryOnePermissions, NextPermissions, Flags;
    public bool GroupOwned;
    public int AssetType, InvType, SalePrice, CreationDate;
    public byte SaleType;
    public string Name;
}

class InventoryServiceStub
{
    public int Calls, Seed, Mode;
    public string Trace = "";
    public InventoryItemBase[] GetMultipleItems(UUID owner, UUID[] ids)
    {
        Calls++;
        Trace = owner.ToString();
        foreach (var id in ids)
            Trace += "," + id;
        if ((Mode & 4) != 0)
            throw new Exception();
        if ((Mode & 8) != 0)
            return null;
        var result = new InventoryItemBase[ids.Length];
        for (int i = 0; i < ids.Length; i++)
            if (!ids[i].IsZero() && i % 3 != 2)
                result[i] = InventoryCapsReference.Sample(Seed, ids[i]);
        return result;
    }
}

partial class InventoryCapsReference
{
    InventoryServiceStub m_inventoryService = new();
    UUID m_agentID = Id(42);
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString().PadLeft(12, '0'));
    public static InventoryItemBase Sample(int seed, UUID key) => new()
    {
        ID = key,
        Folder = seed % 2 != 0 ? Id(900) : UUID.Zero,
        AssetID = Id(500 + seed),
        Owner = Id(42),
        GroupID = seed % 2 != 0 ? Id(43) : UUID.Zero,
        CreatorIdAsUuid = seed % 3 == 0 ? UUID.Zero : Id(44),
        CurrentPermissions = seed % 2 != 0 ? uint.MaxValue : 0,
        GroupPermissions = (uint)(seed * 71),
        EveryOnePermissions = unchecked((uint)-seed),
        NextPermissions = unchecked((uint)(int.MinValue + seed)),
        Flags = unchecked((uint)(-1 - seed)),
        GroupOwned = seed % 2 != 0,
        AssetType = seed - 5,
        InvType = seed % 3,
        SalePrice = seed % 3 != 0 ? seed * 9 : 0,
        SaleType = (byte)(seed % 4),
        Name = seed % 4 == 0 ? "" : "name <&>\"' é",
        Description = seed % 7 == 0 ? "\u2003\t" : seed % 3 != 0 ? "description\n\t<&>\"' 雪" : "",
        CreationDate = seed % 2 != 0 ? -seed : 0
    };
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--timer")
        {
            string command;
            while ((command = Console.ReadLine()) != null)
            {
                var v = command.Split(' ');
                ControlledTimer.Trace = "";
                Util.Clock = 0;
                string result;
                using (var cache = new ExpiringKey<UUID>(30000))
                {
                    Util.Clock = int.Parse(v[0]);
                    cache.Add(Id(1), int.Parse(v[1]));
                    cache.Add(Id(1), int.Parse(v[1]));
                    Util.Clock = int.Parse(v[2]);
                    if (v[3] == "1")
                        typeof(ExpiringKey<UUID>).GetMethod("Purge", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(cache, new object[] { null });
                    Util.Clock = unchecked(int.Parse(v[2]) + 1);
                    cache.Add(Id(2), 0);
                    typeof(ExpiringKey<UUID>).GetMethod("Purge", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(cache, new object[] { null });
                    result = (cache.ContainsKey(Id(1)) ? "1" : "0") + (cache.ContainsKey(Id(2)) ? "1" : "0");
                }

                Console.WriteLine(result + "|" + ControlledTimer.Trace);
            }

            return;
        }

        if (args.Length > 0 && args[0] == "--http")
        {
            string command;
            while ((command = Console.ReadLine()) != null)
            {
                var v = command.Split(' ');
                var core = new InventoryCapsReference();
                var bad = new ExpiringKey<UUID>(30000);
                var wrapper = new Wrapper
                {
                    m_processRequest = (request, response, map) => core.FetchInventorySimpleRequest(request, response, map, bad)
                };
                var response = new IOSHttpResponse();
                var bytes = v[2] == "-" ? Array.Empty<byte>() : Convert.FromHexString(v[2]);
                wrapper.Handle(new IOSHttpRequest { HttpMethod = v[0], InputStream = new MemoryStream(bytes) }, response);
                Console.WriteLine(response.StatusCode + ":" + Convert.ToHexString(response.RawBuffer).ToLowerInvariant() + "|" + wrapper.RequestsReceived + "|" + wrapper.RequestsHandled + "|" + core.m_inventoryService.Calls + "|" + core.m_inventoryService.Trace);
            }

            return;
        }

        if (args.Length > 0 && args[0] == "--cache")
        {
            string command;
            while ((command = Console.ReadLine()) != null)
            {
                var v = command.Split(' ');
                Util.Clock = 0;
                using var cache = new ExpiringKey<UUID>(30000);
                Util.Clock = int.Parse(v[0]);
                cache.Add(Id(1), int.Parse(v[1]));
                Util.Clock = int.Parse(v[2]);
                if (v[3] == "1")
                    typeof(ExpiringKey<UUID>).GetMethod("Purge", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(cache, new object[] { null });
                Console.WriteLine(cache.ContainsKey(Id(1)) ? "1" : "0");
            }

            return;
        }

        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int seed = int.Parse(p[0]), count = int.Parse(p[1]), mode = int.Parse(p[2]);
            var core = new InventoryCapsReference();
            core.m_inventoryService.Seed = seed;
            core.m_inventoryService.Mode = mode;
            var bad = new ExpiringKey<UUID>();
            if ((mode & 1) != 0)
                bad.Add(Id(2));
            var map = new OSDMap();
            var array = new OSDArray();
            for (int i = 0; i < count; i++)
            {
                var entry = new OSDMap();
                if ((mode & 64) == 0)
                    entry["item_id"] = OSD.FromUUID(Id(i + 1));
                array.Add((mode & 32) != 0 ? OSD.FromString("wrong") : entry);
            }

            map["items"] = (mode & 16) != 0 ? OSD.FromString("wrong") : array;
            var response = new IOSHttpResponse();
            string result;
            try
            {
                core.FetchInventorySimpleRequest(new IOSHttpRequest(), response, map, (mode & 2) != 0 ? null : bad);
                result = response.StatusCode + ":" + Convert.ToHexString(response.RawBuffer).ToLowerInvariant();
            }
            catch (Exception)
            {
                result = "CAST";
            }

            Console.WriteLine(result + "|" + core.m_inventoryService.Calls + "|" + core.m_inventoryService.Trace);
        }
    }
}

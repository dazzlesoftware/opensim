// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
static class Program
{
    public static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    static string Hex(string value) => Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    static string Tree(OSD value)
    {
        if (value.Type == OSDType.Array)
            return "a[" + string.Concat(((OSDArray)value).Select(child => Tree(child) + ";")) + "]";
        if (value.Type == OSDType.Map)
        {
            var map = (OSDMap)value;
            return "m{" +
                   string.Concat(map.Keys.OrderBy(key => key, StringComparer.Ordinal)
                                     .Select(key => key + ":" + Tree(map[key]) + ";")) +
                   "}";
        }
        if (value.Type == OSDType.UUID)
            return "q" + value.AsUUID();
        if (value.Type == OSDType.Binary)
            return "x" + Convert.ToHexString(value.AsBinary()).ToLowerInvariant();
        if (value.Type == OSDType.String)
            return "s" + Hex(value.AsString());
        if (value.Type == OSDType.Integer)
            return "i" + value.AsInteger();
        return "u";
    }
    static string Items(WearableCacheItem[] values) =>
        values.Length + "[" +
        string.Concat(values.Select(value => value == null
                                                 ? "null;"
                                                 : value.TextureIndex + ":" + value.CacheId + ":" +
                                                       value.TextureID + ":" +
                                                       (value.TextureAsset != null ? 1 : 0) + ";")) +
        "]";
    class FixtureCache : IAssetCache
    {
        public int Seed;
        public string Trace = "";
        public List<AssetBase> Stored = new();
        public AssetBase Shared;
        public FixtureCache(int profile)
        {
            Seed = profile;
            Shared = new AssetBase(Id(300), (profile & 16) != 0 ? null : "Cached 試験", 0,
                                   (profile & 16) != 0 ? null : "creator&name");
            if (profile % 3 != 0)
                Shared.Data = profile % 3 == 1 ? new byte[] { 1, 2, 3, 4 } : Array.Empty<byte>();
            if ((profile & 32) != 0)
                Shared.Metadata = null;
        }
        public void Cache(AssetBase asset, bool replace = false)
        {
            Trace += "cache:" + asset.ID + ":" + (replace ? 1 : 0) + ";";
            Stored.Add(asset);
            if ((Seed & 8) != 0)
                throw new Exception("cache");
        }
        public void CacheNegative(string id)
        {
        }
        public bool Check(string key)
        {
            Trace += "check:" + key + ";";
            if (Seed % 5 == 3)
                throw new Exception("check");
            return Seed % 5 != 0;
        }
        public bool Get(string key, out AssetBase result)
        {
            Trace += "get:" + key + ";";
            if (Seed % 5 == 4)
                throw new Exception("get");
            result = Seed % 5 == 1 ? null : Shared;
            return Seed % 5 != 2;
        }
        public AssetBase GetCached(string id) => Shared;
        public bool GetFromMemory(string id, out AssetBase result)
        {
            result = Shared;
            return true;
        }
        public void Expire(string id)
        {
        }
        public void Clear()
        {
        }
        public string State() =>
            Trace + "|" +
            string.Concat(Stored.Select(
                asset => asset.ID + ":" + Hex(asset.Name ?? "") + ":" + Hex(asset.CreatorID ?? "") + ":" +
                         asset.Type + ":" + (asset.Temporary ? 1 : 0) + ":" + (asset.Local ? 1 : 0) + ":" +
                         (int)asset.Flags + ":" +
                         (asset.Data == null ? "null" : Convert.ToHexString(asset.Data).ToLowerInvariant()) +
                         ";"));
    }
    static OSDMap Row(uint index, int cache = 10,
                      int texture = 20) => new OSDMap { { "textureindex", OSD.FromUInteger(index) },
                                                        { "cacheid", OSD.FromUUID(Id(cache)) },
                                                        { "textureid", OSD.FromUUID(Id(texture)) } };
    static OSDArray ArrayOf(params OSD[] values)
    {
        var result = new OSDArray();
        foreach (var value in values)
            result.Add(value);
        return result;
    }
    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        int seed = int.Parse(args[0]);
        void Observe(string label, Func<FixtureCache, string> action)
        {
            var cache = new FixtureCache(seed);
            string result;
            try
            {
                result = action(cache);
            }
            catch
            {
                result = "EX";
            }
            Console.WriteLine(label + "|" + result + "|" + cache.State());
        }
        Observe("defaults", cache => Items(WearableCacheItem.GetDefaultCacheItem()));
        var input = Row(8);
        input["assetdata"] = OSD.FromBinary(new byte[] { 7, 8, 9 });
        input["assetcreator"] = OSD.FromString("map creator");
        input["assetname"] = OSD.FromString("map name");
        Observe("from-map", cache => Items(WearableCacheItem.FromOSD(input, cache)));
        Observe("from-array", cache => Items(WearableCacheItem.FromOSD(ArrayOf(input, Row(44)), cache)));
        Observe("from-array-no-cache", cache => Items(WearableCacheItem.FromOSD(ArrayOf(input), null)));
        Observe("from-empty-map", cache => Items(WearableCacheItem.FromOSD(new OSDMap(), cache)));
        Observe("from-wrong", cache => Items(WearableCacheItem.FromOSD(OSD.FromInteger(1), cache)));
        Observe("from-null", cache => Items(WearableCacheItem.FromOSD(null, cache)));
        Observe("from-partial-error",
                cache => Items(WearableCacheItem.FromOSD(ArrayOf(input, OSD.FromInteger(1)), cache)));
        Observe("from-alias", cache =>
                              {
                                  var copy = input;
                                  var data = copy["assetdata"].AsBinary();
                                  var result = WearableCacheItem.FromOSD(copy, cache);
                                  data[0] ^= 85;
                                  bool aliased = cache.Stored.Count > 0 &&
                                                 ReferenceEquals(cache.Stored.Last().Data, data);
                                  data[0] ^= 85;
                                  return Items(result) + ":" + (aliased ? 1 : 0);
                              });
        Observe("baked-from-array", cache => Items(WearableCacheItem.BakedFromOSD(ArrayOf(
                                        Row(8, 10, 20), Row(0, 11, 21), Row(44, 12, 22), Row(45, 13, 23),
                                        Row(uint.MaxValue, 14, 24), Row(8, 15, 25), new OSDMap()))));
        Observe("baked-from-map", cache => Items(WearableCacheItem.BakedFromOSD(input)));
        Observe("baked-from-wrong", cache => Items(WearableCacheItem.BakedFromOSD(OSD.FromInteger(1))));
        Observe("baked-from-null", cache => Items(WearableCacheItem.BakedFromOSD(null)));
        Observe("baked-from-error",
                cache => Items(WearableCacheItem.BakedFromOSD(ArrayOf(Row(0), OSD.FromInteger(1)))));
        int[] lengths = { 0, 5, 9, 29, 45, 50 };
        var values = new WearableCacheItem[lengths[seed % 6]];
        for (int i = 0; i < values.Length; i++)
            values[i] = new WearableCacheItem { TextureIndex = (uint)i, CacheId = Id(100 + i % 3),
                                                TextureID = Id(200 + i % 4), TextureAsset = new AssetBase() };
        Observe("to", cache => Tree(WearableCacheItem.ToOSD(values, cache)));
        Observe("to-no-cache", cache => Tree(WearableCacheItem.ToOSD(values, null)));
        Observe("to-null", cache => Tree(WearableCacheItem.ToOSD(null, cache)));
        Observe("to-alias", cache =>
                            {
                                var packed = WearableCacheItem.ToOSD(values, cache);
                                if (cache.Shared.Data != null && cache.Shared.Data.Length > 0)
                                    cache.Shared.Data[0] ^= 85;
                                return Tree(packed);
                            });
        foreach (var pair in new(int, int)[] { (-1, -1), (0, 6), (6, 11), (3, 3), (5, 2), (99, 100),
                                               (-999, 0), (0, int.MaxValue) })
            Observe("baked-to:" + pair.Item1 + ":" + pair.Item2,
                    cache =>
                    {
                        var packed = WearableCacheItem.BakedToOSD(values, pair.Item1, pair.Item2);
                        return packed == null ? "null" : Tree(packed);
                    });
        foreach (var pair in new(int, int)[] { (0, 0), (2, 1), (0, 1) })
            Observe("baked-null:" + pair.Item1 + ":" + pair.Item2,
                    cache =>
                    {
                        var packed = WearableCacheItem.BakedToOSD(null, pair.Item1, pair.Item2);
                        return packed == null ? "null" : Tree(packed);
                    });
        Observe("search-index", cache =>
                                {
                                    var copy = (WearableCacheItem[])values.Clone();
                                    var found = WearableCacheItem.SearchTextureIndex(0, copy);
                                    if (found != null)
                                        found.TextureIndex = 777;
                                    return (found != null ? 1 : 0) + ":" + Items(copy);
                                });
        Observe("search-cache", cache =>
                                {
                                    var copy = (WearableCacheItem[])values.Clone();
                                    var found = WearableCacheItem.SearchTextureCacheId(Id(101), copy);
                                    if (found != null)
                                        found.TextureIndex = 777;
                                    return (found != null ? 1 : 0) + ":" + Items(copy);
                                });
        Observe("search-texture", cache =>
                                  {
                                      var copy = (WearableCacheItem[])values.Clone();
                                      var found = WearableCacheItem.SearchTextureTextureId(Id(202), copy);
                                      if (found != null)
                                          found.TextureIndex = 777;
                                      return (found != null ? 1 : 0) + ":" + Items(copy);
                                  });
        Observe("search-missing", cache => WearableCacheItem.SearchTextureTextureId(Id(999), values) != null
                                               ? "found"
                                               : "null");
        Observe("search-null",
                cache => WearableCacheItem.SearchTextureIndex(0, null) != null ? "found" : "null");
        Observe("null-entry", cache =>
                              {
                                  var copy = new WearableCacheItem[values.Length + 1];
                                  Array.Copy(values, 0, copy, 1, values.Length);
                                  return Tree(WearableCacheItem.ToOSD(copy, cache));
                              });
        Observe("search-before-null",
                cache =>
                {
                    var copy = WearableCacheItem.GetDefaultCacheItem();
                    copy[1] = null;
                    return ReferenceEquals(WearableCacheItem.SearchTextureIndex(0, copy), copy[0]) ? "same"
                                                                                                   : "other";
                });
        Observe("search-at-null", cache =>
                                  {
                                      var copy = WearableCacheItem.GetDefaultCacheItem();
                                      copy[0] = null;
                                      return WearableCacheItem.SearchTextureIndex(0, copy) != null ? "found"
                                                                                                   : "null";
                                  });
    }
}

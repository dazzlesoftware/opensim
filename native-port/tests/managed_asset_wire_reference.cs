// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Xml.Serialization;
using Nini.Config;
using OpenMetaverse;
using OpenSim.Framework;
using OpenSim.Services.Connectors;

internal static class Program
{
    private static void Check(bool condition, string operation)
    {
        if (!condition)
            throw new InvalidOperationException(operation);
    }

    private static void Main(string[] args)
    {
        if (args[0] == "--metadata-xml")
        {
            WriteMetadataXml(args[1]);
            return;
        }
        if (args[0] == "--read-metadata")
        {
            var serializer = new XmlSerializer(typeof(AssetMetadata));
            ReportMetadata((AssetMetadata)serializer.Deserialize(Console.In));
            return;
        }
        WebUtil.SetupHTTPClients(false, false, null, 16);
        if (args[0] == "--foreign-exists")
        {
            var client = new HGAssetServiceConnector(new IniConfigSource(args[1]));
            Console.WriteLine(string.Concat(client.AssetsExist(args.Skip(2).ToArray())
                .Select(exists => exists ? '1' : '0')));
            return;
        }
        if (args[0] == "--exists" || args[0] == "--exists-config")
        {
            var client = args[0] == "--exists-config"
                ? new AssetServicesConnector(new IniConfigSource(args[1]))
                : new AssetServicesConnector(args[1]);
            Console.WriteLine(string.Concat(client.AssetsExist(new[] { args[2], args[3] })
                .Select(exists => exists ? '1' : '0')));
            return;
        }
        if (args[0] == "--store-reply")
        {
            var client = new AssetServicesConnector(args[1]);
            var upload = new AssetBase(new UUID("91919191-9191-4191-8191-919191919191"),
                                      "Grid write & read", (sbyte)AssetType.LSLText, string.Empty)
            {
                Data = new byte[] { 0, 1, 255 },
            };
            Console.WriteLine(client.Store(upload));
            return;
        }
        if (args[0] == "--mutation-reply")
        {
            var client = new AssetServicesConnector(args[1]);
            bool updated = client.UpdateContent(args[2], new byte[] { 7, 7, 7 });
            bool deleted = client.Delete(args[2]);
            Console.WriteLine($"{(updated ? 1 : 0)}{(deleted ? 1 : 0)}");
            return;
        }
        var config = new IniConfigSource();
        config.AddConfig("Modules");
        var settings = config.AddConfig("AssetService");
        settings.Set("AssetServerURI", args[0]);
        settings.Set("AuthType", "BasicHttpAuthentication");
        settings.Set("HttpAuthUsername", "assets");
        settings.Set("HttpAuthPassword", "secret");
        var grid = new AssetServicesConnector(config);
        var hypergrid = new HGAssetServiceConnector(config);
        var id = new UUID("70707070-7070-4070-8070-707070707070");
        var missing = "80808080-8080-4080-8080-808080808080";
        var data = new byte[] { 0, 1, 128, 255 };
        var asset = new AssetBase(id, "Managed & native é", (sbyte)AssetType.Notecard, missing)
        {
            Description = "Wire format <check>",
            Flags = AssetFlags.Maptile | AssetFlags.Collectable,
            Data = data,
        };
        Check(grid.Store(asset) == id.ToString(), "Store returned an unexpected ID");
        var returned = grid.Get(asset.ID);
        Check(returned != null && returned.FullID == id && returned.ID == asset.ID &&
              returned.Name == asset.Name && returned.Description == asset.Description &&
              returned.CreatorID == asset.CreatorID && returned.Flags == asset.Flags &&
              returned.Data.SequenceEqual(data), "Get changed the asset");
        var metadata = grid.GetMetadata(asset.ID);
        Check(metadata != null && metadata.FullID == id && metadata.ID == asset.ID &&
              metadata.Name == asset.Name && metadata.Type == asset.Type &&
              metadata.Description == asset.Description && metadata.CreatorID == asset.CreatorID &&
              metadata.Flags == asset.Flags && metadata.ContentType == "application/vnd.ll.notecard",
              "GetMetadata changed the asset metadata");
        Check(grid.GetData(asset.ID).SequenceEqual(data), "GetData changed the raw bytes");
        Check(grid.AssetsExist(new[] { missing, asset.ID, asset.ID }).SequenceEqual(new[] { false, true, true }),
              "AssetsExist changed order or duplicates");

        var publicAsset = hypergrid.Get(asset.ID, args[1], false);
        Check(publicAsset != null && publicAsset.FullID == id && publicAsset.Data.SequenceEqual(data),
              "Hypergrid Get did not retrieve the shared asset");
        var publicMetadata = hypergrid.GetMetadata(args[1] + "/" + asset.ID);
        Check(publicMetadata != null && publicMetadata.FullID == id && publicMetadata.Name == asset.Name,
              "Hypergrid GetMetadata did not deserialize the public metadata");

        using var delivered = new ManualResetEventSlim();
        object sender = new object();
        bool callbackMatched = false;
        Check(grid.Get(asset.ID, sender, (key, context, result) =>
        {
            callbackMatched = key == asset.ID && ReferenceEquals(context, sender) &&
                              result != null && result.Data.SequenceEqual(data);
            delivered.Set();
        }), "Asynchronous Get was rejected");
        Check(delivered.Wait(TimeSpan.FromSeconds(10)) && callbackMatched, "Asynchronous Get did not deliver");

        Check(!grid.UpdateContent(asset.ID, new byte[] { 5 }), "SQLite unexpectedly allowed UpdateContent");
        Check(grid.GetData(asset.ID).SequenceEqual(data), "Rejected update changed the asset");
        Check(grid.Delete(asset.ID), "Map tile Delete was rejected");
        Check(!grid.AssetsExist(new[] { asset.ID })[0], "Deleted asset still exists");
        Console.WriteLine("PASS: original managed asset connectors interoperate with native Robust");
    }

    private static void WriteMetadataXml(string kind)
    {
        var metadata = new AssetMetadata
        {
            FullID = new UUID("70707070-7070-4070-8070-707070707070"),
            Name = "Managed metadata",
            Type = (sbyte)AssetType.LSLText,
            ContentType = "application/x-custom-asset",
            SHA1 = new byte[] { 0, 128, 255 },
            CreationDate = new DateTime(2024, 2, 29, 12, 34, 56, Enum.Parse<DateTimeKind>(kind))
                .AddTicks(1234567),
        };
        ReportMetadata(metadata);
        new XmlSerializer(typeof(AssetMetadata)).Serialize(Console.Out, metadata);
    }

    private static void ReportMetadata(AssetMetadata metadata)
    {
        Console.WriteLine(
            metadata.CreationDate.Ticks.ToString(CultureInfo.InvariantCulture) + "|" +
            (int)metadata.CreationDate.Kind + "|" + metadata.ContentType + "|" +
            Convert.ToHexString(metadata.SHA1).ToLowerInvariant());
    }
}

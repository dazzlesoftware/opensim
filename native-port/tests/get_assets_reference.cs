// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Globalization;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Web;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
using OpenSim.Capabilities.Handlers;
using OpenSim.Framework.Servers.HttpServer;
namespace log4net
{
    public interface ILog
    {
        void Warn(object message);
    }
    public class Log : ILog
    {
        public void Warn(object message)
        {
            GetAssetsReference.Warnings += message + ";";
            if ((GetAssetsReference.Flags & 16) != 0)
                throw new Exception("warn");
            var service = GetAssetsReference.CurrentService;
            if ((GetAssetsReference.Flags & 64) != 0 && service.Asset != null)
                service.Asset.Data = null;
            if ((GetAssetsReference.Flags & 128) != 0 && service.Asset != null)
                service.Asset.Metadata = null;
            if ((GetAssetsReference.Flags & 256) != 0 && service.Saved != null)
            {
                try
                {
                    service.Saved(null);
                }
                catch
                {
                }
            }
        }
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type type) => new Log();
    }
}
namespace Nini.Config
{
    public class Unused
    {
    }
}
namespace OpenSim.Framework.Servers
{
    public class Unused
    {
    }
}
namespace OpenSim.Framework.Capabilities
{
    public class Caps
    {
    }
}
namespace OpenSim.Framework.Servers.HttpServer
{
    public class OSHttpRequest
    {
        public string Target;
        public NameValueCollection HeaderValues = new();
        public NameValueCollection Headers
        {
            get {
                if ((GetAssetsReference.Flags & 8) != 0)
                    throw new Exception("range");
                return HeaderValues;
            }
        }
        public Dictionary<string, string> QueryAsDictionary
        {
            get {
                if ((GetAssetsReference.Flags & 2) != 0)
                    throw new Exception("queries");
                if ((GetAssetsReference.Flags & 4) != 0)
                    return null;
                var q = HttpUtility.ParseQueryString(new Uri("http://127.0.0.1" + Target).Query);
                var values = new Dictionary<string, string>();
                for (int i = 0; i < q.Count; ++i)
                {
                    var key = q.GetKey(i);
                    if (!string.IsNullOrEmpty(key))
                        values[key] = q[i];
                }
                return values;
            }
        }
    }
    public class OSHttpResponse
    {
        public OSHttpServer.HttpResponse Native;
        public OSHttpResponse(OSHttpServer.HttpResponse r)
        {
            Native = r;
        }
        public string ContentType
        {
            get => Native.ContentType;
            set => Native.ContentType = value;
        }
        public int StatusCode
        {
            get => (int)Native.Status;
            set => Native.Status = (System.Net.HttpStatusCode)value;
        }
        public bool KeepAlive
        {
            get => Native.Connection == OSHttpServer.ConnectionType.KeepAlive;
            set => Native.Connection =
                value ? OSHttpServer.ConnectionType.KeepAlive : OSHttpServer.ConnectionType.Close;
        }
        public byte[] RawBuffer
        {
            get => Native.RawBuffer;
            set => Native.RawBuffer = value;
        }
        public int RawBufferStart
        {
            get => Native.RawBufferStart;
            set => Native.RawBufferStart = value;
        }
        public int RawBufferLen
        {
            get => Native.RawBufferLen;
            set => Native.RawBufferLen = value;
        }
        public int Priority
        {
            get => Native.Priority;
            set => Native.Priority = value;
        }
        public void AddHeader(string name, string value) => Native.AddHeader(name, value);
    }
}
class Service : IAssetService
{
    public AssetBase Asset;
    public SimpleAssetRetrieved Saved;
    public int Mode, Calls;
    public string Id = "", Foreign;
    public bool StoreLocal = true;
    public void Get(string id, string foreign, bool local, SimpleAssetRetrieved callback)
    {
        ++Calls;
        Id = id;
        Foreign = foreign;
        StoreLocal = local;
        Saved = callback;
        if (Mode == 3)
            throw new Exception("before");
        if (Mode == 1)
            callback(null);
        if (Mode == 6)
        {
            var thread = new Thread(() => callback(Asset));
            thread.Start();
            thread.Join();
        }
        else
            callback(Asset);
        if (Mode == 2)
            callback(null);
        if (Mode == 4)
            throw new Exception("after");
    }
    public AssetBase Get(string id) => throw new NotImplementedException();
    public AssetBase Get(string id, string foreign, bool local) => throw new NotImplementedException();
    public AssetMetadata GetMetadata(string id) => throw new NotImplementedException();
    public byte[] GetData(string id) => throw new NotImplementedException();
    public AssetBase GetCached(string id) => throw new NotImplementedException();
    public bool Get(string id, object sender, AssetRetrieved callback) => throw new NotImplementedException();
    public bool[] AssetsExist(string[] ids) => throw new NotImplementedException();
    public string Store(AssetBase asset) => throw new NotImplementedException();
    public bool UpdateContent(string id, byte[] bytes) => throw new NotImplementedException();
    public bool Delete(string id) => throw new NotImplementedException();
}
class GetAssetsReference
{
    public static int Flags;
    public static string Warnings;
    public static Service CurrentService;
    static string Text(string value) => value == "N" ? null
                                        : value == "."
                                            ? ""
                                            : Encoding.UTF8.GetString(Convert.FromHexString(value));
    static string Hex(string text) => Convert.ToHexString(Encoding.UTF8.GetBytes(text)).ToLowerInvariant();
    static string Token(string value) => value == null ? "N" : "s" + Hex(value);
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        Host.Input = JsonDocument.Parse("{}").RootElement;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                var parts = line.Split(' ');
                string query = parts[0], range = Text(parts[1]);
                int assetMode = int.Parse(parts[2]), type = int.Parse(parts[3]), size = int.Parse(parts[4]),
                    callback = int.Parse(parts[5]), start = int.Parse(parts[6]), length = int.Parse(parts[7]),
                    priority = int.Parse(parts[8]);
                Flags = int.Parse(parts[11]);
                Warnings = "";
                var service = new Service { Mode = callback };
                CurrentService = service;
                if (assetMode != 1)
                {
                    service.Asset = new AssetBase();
                    service.Asset.Type = (sbyte)type;
                    if (parts[10] != "N")
                        service.Asset.Metadata.ContentType = Text(parts[10]);
                    if (assetMode != 2)
                    {
                        service.Asset.Data = new byte[size];
                        for (int i = 0; i < size; ++i)
                            service.Asset.Data[i] = (byte)i;
                    }
                    if (assetMode == 3)
                        service.Asset.Metadata = null;
                }
                var native = new OSHttpServer.HttpResponse(
                    null, "HTTP/1.1",
                    OSHttpServer.ConnectionType.KeepAlive) { Status = (System.Net.HttpStatusCode)207,
                                                             RawBufferStart = start,
                                                             RawBufferLen = length,
                                                             Priority = priority,
                                                             RawBuffer = new byte[] { 0xaa },
                                                             ContentType = "initial/type" };
                var response = new OSHttpResponse(native);
                var handler = new GetAssetsHandler((Flags & 1) != 0 ? null : service);
                OSHttpRequest request = null;
                if (query != "N")
                {
                    request = new OSHttpRequest { Target = Text(query) };
                    if (range != null)
                        request.HeaderValues["range"] = range;
                }
                string result = "OK", late = "-";
                try
                {
                    handler.Handle(request, (Flags & 32) != 0 ? null : response, Text(parts[9]));
                }
                catch
                {
                    result = "EX";
                }
                if ((callback == 3 || callback == 4 || callback == 5) && service.Saved != null)
                {
                    try
                    {
                        service.Saved(null);
                        late = "OK";
                    }
                    catch
                    {
                        late = "EX";
                    }
                }
                if ((Flags & 1024) != 0 && service.Asset?.Data?.Length > 0)
                    service.Asset.Data[0] = 0xee;
                var headers = (NameValueCollection) typeof(OSHttpServer.HttpResponse)
                                  .GetField("m_headers", BindingFlags.NonPublic | BindingFlags.Instance)
                                  .GetValue(native);
                Console.WriteLine(result + "|" + (int)native.Status + "|" + (int)native.Connection + "|" +
                                  native.RawBufferStart + "|" + native.RawBufferLen + "|" + native.Priority +
                                  "|" + Token(native.ContentType) + "|" +
                                  (native.RawBuffer == null
                                       ? "N"
                                       : Convert.ToHexString(native.RawBuffer).ToLowerInvariant()) +
                                  "|" + Token(headers["Content-Range"]) + "|" + service.Calls + "|" +
                                  Hex(service.Id) + "|" + Token(service.Foreign) + "|" +
                                  (service.StoreLocal ? 1 : 0) + "|" + Hex(Warnings) + "|" + late);
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(line + " => " + error);
                Console.WriteLine("FIXTURE_EX");
            }
        }
    }
}

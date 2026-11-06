// Calls original LLLoginHandlers and LLFailedLoginResponse implementations.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using System.Text.Json;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Services.Interfaces;
using OpenSim.Server.Handlers.Login;
using OpenSim.Services.LLLoginService;
using Nwc.XmlRpc;
sealed class Reply : LoginResponse
{
    readonly Hashtable values;
    public Reply(Hashtable input)
    {
        values = input;
    }
    public override Hashtable ToHashtable() => values;
    public override OSD ToOSDMap()
    {
        var result = new OSDMap();
        foreach (DictionaryEntry pair in values)
            result[(string)pair.Key] = OSD.FromString(pair.Value.ToString());
        return result;
    }
}
sealed class Echo : ILoginService
{
    public LoginResponse Login(string first, string last, string password, string start, UUID scope,
                               string version, string channel, string mac, string id0,
                               IPEndPoint ip) => new Reply(new Hashtable { { "first", first },
                                                                           { "last", last },
                                                                           { "passwd", password },
                                                                           { "start", start },
                                                                           { "scope_id", scope.ToString() },
                                                                           { "version", version },
                                                                           { "channel", channel },
                                                                           { "mac", mac },
                                                                           { "id0", id0 },
                                                                           { "ip", ip.Address.ToString() } });
    public Hashtable SetLevel(string first, string last, string password, int level,
                              IPEndPoint ip) => new Hashtable {
        { "first", first }, { "last", last }, { "passwd", password }, { "level", level }, { "success", true }
    };
}
static class Program
{
    static void Main()
    {
        OpenSim.Framework.Culture.SetCurrentCulture();
        OpenSim.Framework.Culture.SetDefaultCurrentCulture();
        Console.InputEncoding = Encoding.UTF8;
        Console.OutputEncoding = new UTF8Encoding(false);
        var handlers = new LLLoginHandlers(new Echo());
        var endpoint = new IPEndPoint(IPAddress.Loopback, 1234);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                int split = line.IndexOf('\t');
                string mode = line[..split], body = line[(split + 1)..];
                object result;
                if (mode == "failure")
                    result =
                        ((LLFailedLoginResponse) typeof(LLFailedLoginResponse).GetField(body).GetValue(null))
                            .ToHashtable();
                else if (mode == "levelraw")
                {
                    var chars = new char[body.Length / 4];
                    for (int i = 0; i < chars.Length; i++)
                        chars[i] = (char)Convert.ToUInt16(body.Substring(i * 4, 4), 16);
                    var request = new XmlRpcRequest();
                    request.Params.Add(new Hashtable { { "first", "Test" },
                                                       { "last", "User" },
                                                       { "passwd", "secret" },
                                                       { "level", new string(chars) } });
                    result = handlers.HandleXMLRPCSetLoginLevel(request, endpoint).Value;
                }
                else if (mode == "llsd")
                {
                    var value = OSDParser.DeserializeLLSDXml(new MemoryStream(Encoding.UTF8.GetBytes(body)));
                    var map = (OSDMap)handlers.HandleLLSDLogin(value, endpoint);
                    var dict = new Dictionary<string, string>();
                    foreach (KeyValuePair<string, OSD> pair in map)
                        dict[pair.Key] = pair.Value.AsString();
                    result = dict;
                }
                else
                {
                    var request =
                        (XmlRpcRequest) new XmlRpcRequestDeserializer().Deserialize(new StringReader(body));
                    request.Params.Add(endpoint);
                    request.Params.Add("http://localhost/");
                    request.Params.Add(null);
                    result = (mode == "level" ? handlers.HandleXMLRPCSetLoginLevel(request, endpoint)
                                              : handlers.HandleXMLRPCLogin(request, endpoint))
                                 .Value;
                }
                Console.WriteLine(JsonSerializer.Serialize(result));
            }
            catch (Exception)
            {
                Console.WriteLine("EXCEPTION");
            }
        }
    }
}

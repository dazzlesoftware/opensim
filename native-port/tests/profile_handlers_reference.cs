using System;
using System.Text;
using System.Reflection;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using OpenSim.Framework.Servers.HttpServer;
using OpenSim.Services.Interfaces;
using OpenSim.Server.Handlers;
namespace log4net
{
    public interface ILog
    {
        void Debug(object message);
        void DebugFormat(string format, params object[] args);
        void InfoFormat(string format, params object[] args);
    }
    public static class LogManager
    {
        public static ILog GetLogger(object t) => Fixture.Log;
    }
}
namespace OpenSim.Framework
{
    public class GroupMembershipData
    {
    }
    public interface IClientAPI
    {
    }
}
class Logger : log4net.ILog
{
    public string Trace = "";
    public void Debug(object m)
    {
        Trace += "D:" + m + "\n";
    }
    public void DebugFormat(string f, params object[] a) => Debug(string.Format(f, a));
    public void InfoFormat(string f, params object[] a)
    {
        Trace += "I:" + string.Format(f, a) + "\n";
    }
}
static class Fixture
{
    public static Logger Log = new();
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Snap(OSD v)
    {
        if (v == null)
            return "N";
        try
        {
            return H(OSDParser.SerializeJsonString(v, true));
        }
        catch
        {
            return "SEREX";
        }
    }
}
class Provider : IUserProfilesService
{
    public int Mode;
    public string Trace = "";
    public Provider(int m)
    {
        Mode = m;
    }
    bool Called(string name, string data)
    {
        Trace += name + ":" + data + ";";
        if (Mode == 4)
            throw new Exception("provider failure");
        return Mode != 1;
    }
    bool Obj<T>(string name, ref T p, ref string result)
        where T : class, new()
    {
        result = Mode == 5 ? null : "service result";
        bool ok = Called(name, p == null ? "N" : Fixture.Snap(OSD.SerializeMembers(p)));
        if (Mode == 2)
            p = new T();
        if (Mode == 3)
            p = null;
        if (p != null)
        {
            object value = p;
            OSD.DeserializeMembers(ref value, new OSDMap { { "Name", OSD.FromString("service") },
                                                           { "Flags", OSD.FromInteger(1) },
                                                           { "Notes", OSD.FromString("service") },
                                                           { "EMail", OSD.FromString("service") },
                                                           { "DataVal", OSD.FromString("service") } });
        }
        return ok;
    }
    OSD List(string name, UUID id)
    {
        Called(name, id.ToString());
        if (Mode == 7)
            return null;
        if (Mode == 6)
            return OSD.FromString("wrong");
        return new OSDArray { OSD.FromString("entry"), OSD.FromString(id.ToString()) };
    }
    public OSD AvatarClassifiedsRequest(UUID id) => List("AvatarClassifiedsRequest", id);
    public bool ClassifiedUpdate(UserClassifiedAdd p, ref string result) => Obj("ClassifiedUpdate", ref p,
                                                                                ref result);
    public bool ClassifiedInfoRequest(ref UserClassifiedAdd p,
                                      ref string result) => Obj("ClassifiedInfoRequest", ref p, ref result);
    public bool ClassifiedDelete(UUID id) => Called("ClassifiedDelete", id.ToString());
    public OSD AvatarPicksRequest(UUID id) => List("AvatarPicksRequest", id);
    public bool PickInfoRequest(ref UserProfilePick p, ref string result) => Obj("PickInfoRequest", ref p,
                                                                                 ref result);
    public bool PicksUpdate(ref UserProfilePick p, ref string result) => Obj("PicksUpdate", ref p,
                                                                             ref result);
    public bool PicksDelete(UUID id) => Called("PicksDelete", id.ToString());
    public bool AvatarNotesRequest(ref UserProfileNotes p)
    {
        string result = "";
        return Obj("AvatarNotesRequest", ref p, ref result);
    }
    public bool NotesUpdate(ref UserProfileNotes p, ref string result) => Obj("NotesUpdate", ref p,
                                                                              ref result);
    public bool AvatarPropertiesRequest(ref UserProfileProperties p,
                                        ref string result) => Obj("AvatarPropertiesRequest", ref p,
                                                                  ref result);
    public bool AvatarPropertiesUpdate(ref UserProfileProperties p,
                                       ref string result) => Obj("AvatarPropertiesUpdate", ref p, ref result);
    public bool AvatarInterestsUpdate(UserProfileProperties p,
                                      ref string result) => Obj("AvatarInterestsUpdate", ref p, ref result);
    public bool UserPreferencesUpdate(ref UserPreferences p,
                                      ref string result) => Obj("UserPreferencesUpdate", ref p, ref result);
    public bool UserPreferencesRequest(ref UserPreferences p,
                                       ref string result) => Obj("UserPreferencesRequest", ref p, ref result);
    public OSD AvatarImageAssetsRequest(UUID id) => List("AvatarImageAssetsRequest", id);
    public bool RequestUserAppData(ref UserAppData p, ref string result) => Obj("RequestUserAppData", ref p,
                                                                                ref result);
    public bool SetUserAppData(UserAppData p, ref string result) => Obj("SetUserAppData", ref p, ref result);
}
class Program
{
    static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        string[] methods = { "AvatarClassifiedsRequest",
                             "ClassifiedUpdate",
                             "ClassifiedInfoRequest",
                             "ClassifiedDelete",
                             "AvatarPicksRequest",
                             "PickInfoRequest",
                             "PicksUpdate",
                             "PicksDelete",
                             "AvatarNotesRequest",
                             "NotesUpdate",
                             "AvatarPropertiesRequest",
                             "AvatarPropertiesUpdate",
                             "AvatarInterestsUpdate",
                             "UserPreferenecesUpdate",
                             "UserPreferencesRequest",
                             "AvatarImageAssetsRequest",
                             "RequestUserAppData",
                             "UpdateUserAppData" };
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            int method = int.Parse(a[0]), mode = int.Parse(a[1]), seed = int.Parse(a[2]);
            if (method >= 18)
            {
                try
                {
                    string text = ((OSDMap)OSDParser.DeserializeJson(
                        Encoding.UTF8.GetString(Convert.FromHexString(a[3])))) ["text"]
                                      .AsString();
                    UUID id;
                    bool ok = true;
                    if (method == 18)
                        id = new UUID(text);
                    else
                        ok = UUID.TryParse(text, out id);
                    Console.WriteLine((ok ? "1:" : "0:") + id);
                }
                catch (Exception e)
                {
                    Console.WriteLine("EX:" + Fixture.H(e.Message));
                }
                continue;
            }
            Fixture.Log.Trace = "";
            var provider = new Provider(mode);
            var handlers = new JsonRpcProfileHandlers(seed == 4 ? null : provider);
            var response = new JsonRpcResponse { Id = "wireid", JsonRpc = "2.0" };
            if (seed == 1)
            {
                response.Error.Code = 17;
                response.Error.Message = "existing";
                response.Result = OSD.FromString("existing");
            }
            if (seed == 2)
                response.Error = null;
            if (seed == 3)
                response = null;
            string status;
            try
            {
                var json =
                    (OSDMap)OSDParser.DeserializeJson(Encoding.UTF8.GetString(Convert.FromHexString(a[3])));
                if (json.ContainsKey("__null"))
                    json["params"] = null;
                object[] args = { json, response };
                try
                {
                    status =
                        (bool)typeof(JsonRpcProfileHandlers).GetMethod(methods[method]).Invoke(handlers, args)
                            ? "1"
                            : "0";
                }
                finally
                {
                    response = (JsonRpcResponse)args[1];
                }
            }
            catch (Exception ex)
            {
                if (ex is TargetInvocationException)
                    ex = ex.InnerException;
                status = "EX:" + Fixture.H(ex.Message);
            }
            string error =
                response == null ? "N"
                : response.Error == null
                    ? "N"
                    : response.Error.Code + ":" +
                          (response.Error.Message == null ? "N" : Fixture.H(response.Error.Message));
            string wire = "SEREX";
            try
            {
                if (response != null)
                    wire = Fixture.H(response.Serialize());
            }
            catch
            {
            }
            Console.WriteLine(status + "|" + error + "|" +
                              (response == null ? "N" : Fixture.Snap(response.Result)) + "|" +
                              Fixture.H(provider.Trace) + "|" + Fixture.H(Fixture.Log.Trace) + "|" + wire);
        }
    }
}

using System;
using System.Text;
using System.Reflection;
using Nini.Config;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
using OpenSim.Data;
namespace log4net
{
    public interface ILog
    {
        void Info(object m);
        void ErrorFormat(string f, params object[] a);
        void Error(object m);
        void Warn(object m);
        void WarnFormat(string f, params object[] a);
        void Debug(object m);
        void DebugFormat(string f, params object[] a);
        void InfoFormat(string f, params object[] a);
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
namespace OpenSim.Services.Interfaces
{
    public class UserAccount
    {
        public string Email;
    }
    public interface IUserAccountService
    {
        UserAccount GetUserAccount(UUID scope, UUID user);
    }
}
namespace OpenSim.Server.Base
{
    public static class ServerUtils
    {
        public static T LoadPlugin<T>(string name, object[] args)
            where T : class => (Fixture.PluginOverride == null ? Fixture.Accounts : Fixture.PluginOverride(name,
                                                                                    args)) as T;
    }
}
namespace OpenSim.Services.UserAccountService
{
    public class UserAccountService : IUserAccountService
    {
        public virtual UserAccount GetUserAccount(UUID scope, UUID user) => Fixture.Account(scope, user);
    }
    public class Derived : UserAccountService
    {
    }
}
class Remote : IUserAccountService
{
    public UserAccount GetUserAccount(UUID scope, UUID user) => Fixture.Account(scope, user);
}
class Logger : log4net.ILog
{
    public string Trace = "";
    public void Info(object m)
    {
        Trace += "INFO:" + m + "\n";
    }
    public void ErrorFormat(string f, params object[] a) => Error(string.Format(f, a));
    public void WarnFormat(string f, params object[] a) => Warn(string.Format(f, a));
    public void Debug(object m)
    {
    }
    public void DebugFormat(string f, params object[] a)
    {
    }
    public void InfoFormat(string f, params object[] a)
    {
    }
    public void Error(object m)
    {
        Trace += m + "\n";
    }
    public void Warn(object m)
    {
        Trace += "WARN:" + m + "\n";
    }
}
static class Fixture
{
    public static Func<string, object[], object> PluginOverride;
    public static Logger Log = new();
    public static Store Data;
    public static IUserAccountService Accounts;
    public static int AccountMode;
    public static string AccountTrace = "";
    public static UserAccount Account(UUID scope, UUID user)
    {
        AccountTrace += scope + ":" + user + ";";
        if (AccountMode == 2)
            return null;
        if (AccountMode == 3)
            throw new Exception("account failure");
        return new UserAccount { Email = AccountMode == 4   ? ""
                                         : AccountMode == 5 ? null
                                                            : "account@example.test" };
    }
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Snap(OSD v) => v == null ? "N" : H(OSDParser.SerializeJsonString(v, true));
    public static string Obj(object v) => v == null ? "N" : Snap(OSD.SerializeMembers(v));
}
class Store : IProfilesData
{
    public int Mode;
    public string Trace = "";
    public Store(int m)
    {
        Mode = m;
    }
    bool Called(string name, string value)
    {
        Trace += name + ":" + value + ";";
        if (Mode == 4 || (Mode == 9 && name == "UpdateUserPreferences"))
            throw new Exception("provider failure");
        return Mode != 1 && !(Mode == 8 && name == "UpdateUserPreferences");
    }
    bool Obj<T>(string name, ref T value, ref string result)
        where T : class, new()
    {
        bool ok = Called(name, Fixture.Obj(value));
        result = Mode == 5 ? null : "provider result";
        if (Mode == 2)
            value = new T();
        if (Mode == 3)
            value = null;
        if (value != null)
        {
            object obj = value;
            var fields = new OSDMap { { "Name", OSD.FromString("stored") },
                                      { "Notes", OSD.FromString("stored") },
                                      { "DataVal", OSD.FromString("stored") } };
            if (Mode == 7)
                fields["EMail"] = new OSD();
            OSD.DeserializeMembers(ref obj, fields);
        }
        return ok;
    }
    OSDArray List(string name, UUID id)
    {
        Called(name, id.ToString());
        if (Mode == 6)
            return null;
        return new OSDArray { OSD.FromString(id.ToString()) };
    }
    public OSDArray GetClassifiedRecords(UUID id) => List("GetClassifiedRecords", id);
    public bool UpdateClassifiedRecord(UserClassifiedAdd value,
                                       ref string result) => Obj("UpdateClassifiedRecord", ref value,
                                                                 ref result);
    public bool GetClassifiedInfo(ref UserClassifiedAdd value,
                                  ref string result) => Obj("GetClassifiedInfo", ref value, ref result);
    public bool DeleteClassifiedRecord(UUID id) => Called("DeleteClassifiedRecord", id.ToString());
    public OSDArray GetAvatarPicks(UUID id) => List("GetAvatarPicks", id);
    public UserProfilePick GetPickInfo(UUID avatar, UUID pick)
    {
        Called("GetPickInfo", avatar + ":" + pick);
        if (Mode == 3)
            return null;
        return new UserProfilePick { Name = "stored" };
    }
    public bool UpdatePicksRecord(UserProfilePick value)
    {
        string result = "";
        return Obj("UpdatePicksRecord", ref value, ref result);
    }
    public bool DeletePicksRecord(UUID id) => Called("DeletePicksRecord", id.ToString());
    public bool GetAvatarNotes(ref UserProfileNotes value)
    {
        string result = "";
        return Obj("GetAvatarNotes", ref value, ref result);
    }
    public bool UpdateAvatarNotes(ref UserProfileNotes value,
                                  ref string result) => Obj("UpdateAvatarNotes", ref value, ref result);
    public bool GetAvatarProperties(ref UserProfileProperties value,
                                    ref string result) => Obj("GetAvatarProperties", ref value, ref result);
    public bool UpdateAvatarProperties(ref UserProfileProperties value,
                                       ref string result) => Obj("UpdateAvatarProperties", ref value,
                                                                 ref result);
    public bool UpdateAvatarInterests(UserProfileProperties value,
                                      ref string result) => Obj("UpdateAvatarInterests", ref value,
                                                                ref result);
    public bool UpdateUserPreferences(ref UserPreferences value,
                                      ref string result) => Obj("UpdateUserPreferences", ref value,
                                                                ref result);
    public bool GetUserPreferences(ref UserPreferences value,
                                   ref string result) => Obj("GetUserPreferences", ref value, ref result);
    public OSDArray GetUserImageAssets(UUID id) => List("GetUserImageAssets", id);
    public bool GetUserAppData(ref UserAppData value, ref string result) => Obj("GetUserAppData", ref value,
                                                                                ref result);
    public bool SetUserAppData(UserAppData value, ref string result) => Obj("SetUserAppData", ref value,
                                                                            ref result);
}
class Program
{
    static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        string[] names = { "AvatarClassifiedsRequest",
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
                           "UserPreferencesUpdate",
                           "UserPreferencesRequest",
                           "AvatarImageAssetsRequest",
                           "RequestUserAppData",
                           "SetUserAppData" };
        Type[] types = { null,
                         typeof(UserClassifiedAdd),
                         typeof(UserClassifiedAdd),
                         null,
                         null,
                         typeof(UserProfilePick),
                         typeof(UserProfilePick),
                         null,
                         typeof(UserProfileNotes),
                         typeof(UserProfileNotes),
                         typeof(UserProfileProperties),
                         typeof(UserProfileProperties),
                         typeof(UserProfileProperties),
                         typeof(UserPreferences),
                         typeof(UserPreferences),
                         null,
                         typeof(UserAppData),
                         typeof(UserAppData) };
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var parts = line.Split(' ');
            int method = int.Parse(parts[0]), mode = int.Parse(parts[1]), email = int.Parse(parts[2]),
                account = int.Parse(parts[3]), flags = int.Parse(parts[4]), seed = int.Parse(parts[5]);
            Fixture.Log.Trace = Fixture.AccountTrace = "";
            Fixture.AccountMode = account;
            var store = new Store(mode);
            Fixture.Data = (flags & 2) != 0 ? null : store;
            Fixture.Accounts = account == 0   ? null
                               : account == 1 ? new Remote()
                               : account == 7 ? new OpenSim.Services.UserAccountService.Derived()
                                              : new OpenSim.Services.UserAccountService.UserAccountService();
            var config = new IniConfigSource();
            config.AddConfig("Profiles").Set("UserAccountService", "account");
            var service = new OpenSim.Services.ProfilesService.UserProfilesService(config, "Profiles");
            string result = seed != 0 ? null : "initial", status = "", snapshot = "-";
            object value = null;
            if (types[method] != null && (flags & 1) == 0)
            {
                value = Activator.CreateInstance(types[method]);
                OSD.DeserializeMembers(ref value, new OSDMap {
                    { "UserId", OSD.FromString("00000000-0000-0000-0000-000000000001") },
                    { "CreatorId", OSD.FromString("00000000-0000-0000-0000-000000000002") },
                    { "PickId", OSD.FromString("00000000-0000-0000-0000-000000000003") },
                    { "EMail", email == 0 ? new OSD()
                                          : OSD.FromString(email == 1   ? ""
                                                           : email == 2 ? " "
                                                                        : "incoming@example.test") }
                });
            }
            Action call = null;
            switch (method)
            {
            case 0:
                call = () =>
                {
                    snapshot = Fixture.Snap(
                        service.AvatarClassifiedsRequest(new UUID("00000000-0000-0000-0000-000000000001")));
                    status = "O";
                };
                break;
            case 1: {
                var typed = (UserClassifiedAdd)value;
                call = () =>
                {
                    try
                    {
                        status = service.ClassifiedUpdate(typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 2: {
                var typed = (UserClassifiedAdd)value;
                call = () =>
                {
                    try
                    {
                        status = service.ClassifiedInfoRequest(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 3:
                call = () =>
                {
                    status = service.ClassifiedDelete(new UUID("00000000-0000-0000-0000-000000000001")) ? "1"
                                                                                                        : "0";
                };
                break;
            case 4:
                call = () =>
                {
                    snapshot = Fixture.Snap(
                        service.AvatarPicksRequest(new UUID("00000000-0000-0000-0000-000000000001")));
                    status = "O";
                };
                break;
            case 5: {
                var typed = (UserProfilePick)value;
                call = () =>
                {
                    try
                    {
                        status = service.PickInfoRequest(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 6: {
                var typed = (UserProfilePick)value;
                call = () =>
                {
                    try
                    {
                        status = service.PicksUpdate(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 7:
                call = () =>
                {
                    status =
                        service.PicksDelete(new UUID("00000000-0000-0000-0000-000000000001")) ? "1" : "0";
                };
                break;
            case 8: {
                var typed = (UserProfileNotes)value;
                call = () =>
                {
                    try
                    {
                        status = service.AvatarNotesRequest(ref typed) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 9: {
                var typed = (UserProfileNotes)value;
                call = () =>
                {
                    try
                    {
                        status = service.NotesUpdate(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 10: {
                var typed = (UserProfileProperties)value;
                call = () =>
                {
                    try
                    {
                        status = service.AvatarPropertiesRequest(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 11: {
                var typed = (UserProfileProperties)value;
                call = () =>
                {
                    try
                    {
                        status = service.AvatarPropertiesUpdate(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 12: {
                var typed = (UserProfileProperties)value;
                call = () =>
                {
                    try
                    {
                        status = service.AvatarInterestsUpdate(typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 13: {
                var typed = (UserPreferences)value;
                call = () =>
                {
                    try
                    {
                        status = service.UserPreferencesUpdate(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 14: {
                var typed = (UserPreferences)value;
                call = () =>
                {
                    try
                    {
                        status = service.UserPreferencesRequest(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 15:
                call = () =>
                {
                    snapshot = Fixture.Snap(
                        service.AvatarImageAssetsRequest(new UUID("00000000-0000-0000-0000-000000000001")));
                    status = "O";
                };
                break;
            case 16: {
                var typed = (UserAppData)value;
                call = () =>
                {
                    try
                    {
                        status = service.RequestUserAppData(ref typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            case 17: {
                var typed = (UserAppData)value;
                call = () =>
                {
                    try
                    {
                        status = service.SetUserAppData(typed, ref result) ? "1" : "0";
                    }
                    finally
                    {
                        value = typed;
                    }
                };
                break;
            }
            }
            try
            {
                call();
            }
            catch (Exception ex)
            {
                status = "EX:" + Fixture.H(ex.Message);
            }
            if (types[method] != null)
                snapshot = Fixture.Obj(value);
            Console.WriteLine(status + "|" + snapshot + "|" + (result == null ? "N" : Fixture.H(result)) +
                              "|" + Fixture.H(store.Trace) + "|" + Fixture.H(Fixture.AccountTrace) + "|" +
                              Fixture.H(Fixture.Log.Trace));
        }
    }
}

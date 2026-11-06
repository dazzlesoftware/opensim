using System;
using System.Text;
using System.Collections.Generic;
using System.Reflection;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using OpenSim.Framework.Servers.HttpServer;
namespace log4net
{
    public interface ILog
    {
        void Debug(object message, Exception e);
        void DebugFormat(string format, params object[] args);
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
    public static class WebUtil
    {
        public static OSDMap PostToService(string uri, OSDMap request, int timeout,
                                           bool rpc) => Fixture.Post(uri, request, timeout, rpc);
    }
}
class Log : log4net.ILog
{
    public string Trace = "";
    public void Debug(object message, Exception e)
    {
        Trace += message + " EX:" + e.Message + "\n";
    }
    public void DebugFormat(string format, params object[] args)
    {
        Trace += string.Format(format, args) + "\n";
    }
}
static class Fixture
{
    public static Log Log = new Log();
    public static int Calls, Mode, Args;
    public static string Kind, Captured = "-", Wire = "-", PostInfo = "-";
    static Type[] Types = { typeof(UserClassifiedAdd), typeof(UserProfileProperties),
                            typeof(UserProfilePick),   typeof(UserProfileNotes),
                            typeof(UserPreferences),   typeof(UserAccountProperties),
                            typeof(UserAccountAuth),   typeof(UserAppData) };
    public static UUID Id(int i) => new UUID("00000000-0000-0000-0000-" + i.ToString("x12"));
    public static object Model(int type, bool fill)
    {
        if (type >= Types.Length)
            return null;
        object value = Activator.CreateInstance(Types[type]);
        if (fill)
        {
            int i = 0;
            foreach (var f in Types[type].GetFields())
            {
                object field = f.FieldType == typeof(UUID)   ? (object)Id(i + 1)
                               : f.FieldType == typeof(int)  ? (object)(i - 7)
                               : f.FieldType == typeof(byte) ? (object)(byte)(255 - i)
                               : f.FieldType == typeof(bool) ? (object)(i % 2 == 0)
                               : i % 3 == 0                  ? null
                                                             : (object)("initial <&> " + f.Name + " é");
                f.SetValue(value, field);
                ++i;
            }
        }
        return value;
    }
    static List<FieldInfo> Schema()
    {
        var list = new List<FieldInfo>();
        var names = new HashSet<string>();
        foreach (var type in Types)
            foreach (var field in type.GetFields())
                if (names.Add(field.Name))
                    list.Add(field);
        return list;
    }
    static OSDMap MemberResult(int mode)
    {
        var map = new OSDMap();
        int i = 0;
        foreach (var field in Schema())
        {
            OSD value = field.FieldType == typeof(UUID)   ? OSD.FromUUID(Id(1000 + i))
                        : field.FieldType == typeof(int)  ? OSD.FromString("17.9")
                        : field.FieldType == typeof(byte) ? OSD.FromInteger(-1)
                        : field.FieldType == typeof(bool) ? OSD.FromString("0")
                                                          : OSD.FromString("remote <&> " + field.Name + " é");
            if (mode == 13)
                value = new OSD();
            if (mode == 14 && field.Name == "Name")
                value = null;
            map[field.Name] = value;
            ++i;
        }
        return map;
    }
    public static OSD Initial(int kind)
    {
        switch (kind)
        {
        case 0:
            return null;
        case 1:
            return new OSD();
        case 2:
            return new OSDMap { { "x", OSD.FromInteger(1) }, { "zero", OSD.FromInteger(0) } };
        case 3:
            return new OSDArray { OSD.FromInteger(1), new OSD() };
        case 4:
            return OSD.FromString("parameter <&> é");
        case 5:
            return OSD.FromInteger(0);
        case 6:
            return OSD.FromBinary(new byte[] { 0, 1, 2, 3 });
        default:
            return OSD.FromUUID(UUID.Zero);
        }
    }
    public static OSDMap Post(string uri, OSDMap request, int timeout, bool rpc)
    {
        ++Calls;
        PostInfo = timeout + ":" + (rpc ? "1" : "0") + ":" + (uri == null ? "N" : H(uri));
        if (Kind == "O" && (Args == 1 || Args == 2 || Args == 8))
        {
            string generated = request["id"].AsString();
            if (generated.Length != 36 || generated[14] != '4' || !"89ab".Contains(generated[19]))
                throw new Exception("bad generated id");
            request["id"] = OSD.FromString("<generated>");
        }
        Captured = H(OSDParser.SerializeJsonString(request, true));
        Wire = H(OSDParser.SerializeJsonString(request));
        if (Mode == 9)
            throw new Exception("post failed");
        if (Mode == 10)
            return null;
        var outer = new OSDMap();
        if (Mode == 1)
            return outer;
        if (Mode == 2)
        {
            outer["_Result"] = OSD.FromString("bad");
            return outer;
        }
        if (Mode == 11)
        {
            outer["_Result"] = null;
            return outer;
        }
        if (Mode == 17)
        {
            outer["_Result"] = request["params"];
            return outer;
        }
        var response = new OSDMap();
        if (Mode != 3)
        {
            response["jsonrpc"] = OSD.FromString("2.0");
            response["id"] = OSD.FromString("reply");
            if (Mode == 4)
                response["error"] =
                    new OSDMap { { "code", OSD.FromInteger(-3) }, { "message", OSD.FromString("denied") } };
            else if (Mode == 5)
                response["error"] = new OSD();
            else if (Mode == 6)
                response["error"] = null;
            else if (Mode == 7)
                response["result"] = OSD.FromString("bad");
            else if (Mode == 8)
                response["result"] = null;
            else if (Mode == 12)
                response["result"] = new OSDMap();
            else
                response["result"] = MemberResult(Mode);
        }
        outer["_Result"] =
            Mode == 18 ? OSDParser.Deserialize(OSDParser.SerializeJsonString(response)) : response;
        if (Mode == 16)
            outer["Success"] = OSD.FromBoolean(false);
        return outer;
    }
    public static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Snapshot(OSD value)
    {
        if (value == null)
            return "N";
        try
        {
            return H(OSDParser.SerializeJsonString(value, true));
        }
        catch
        {
            return "SEREX";
        }
    }
    public static string SnapshotObject(object value)
    {
        if (value == null)
            return "N";
        try
        {
            return Snapshot(OSD.SerializeMembers(value));
        }
        catch
        {
            return "SEREX";
        }
    }
}
class Program
{
    static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            var parts = row.Split(' ');
            Fixture.Kind = parts[0];
            int type = int.Parse(parts[1]), fill = int.Parse(parts[2]);
            Fixture.Mode = int.Parse(parts[3]);
            Fixture.Args = int.Parse(parts[4]);
            Fixture.Calls = 0;
            Fixture.Captured = Fixture.Wire = Fixture.PostInfo = "-";
            Fixture.Log.Trace = "";
            string method = "profile.query", uri = "http://service.test/rpc", id = "id <&>";
            switch (Fixture.Args)
            {
            case 1:
                id = "";
                break;
            case 2:
                id = null;
                break;
            case 3:
                uri = "";
                break;
            case 4:
                uri = " \u2003\t";
                break;
            case 5:
                uri = null;
                break;
            case 6:
                method = "";
                break;
            case 7:
                method = null;
                break;
            case 8:
                id = uri = method = null;
                break;
            case 9:
                id = " ";
                break;
            }
            OSD data = Fixture.Initial(type), originalData = data;
            object value = Fixture.Model(type, fill != 0), originalObject = value;
            string status;
            var manager = new JsonRpcRequestManager();
            try
            {
                status = (Fixture.Kind == "O" ? manager.JsonRpcRequest(ref data, method, uri, id)
                                              : manager.JsonRpcRequest(ref value, method, uri, id))
                             ? "1"
                             : "0";
            }
            catch (ArgumentNullException e)
            {
                status = "ARG:" + e.ParamName;
            }
            catch (Exception e)
            {
                status = "EX:" + Fixture.H(e.Message);
            }
            bool same = Fixture.Kind == "O" ? ReferenceEquals(data, originalData)
                                            : ReferenceEquals(value, originalObject);
            Console.WriteLine(status + "|" +
                              (Fixture.Kind == "O" ? Fixture.Snapshot(data) : Fixture.SnapshotObject(value)) +
                              "|" + (same ? "S" : "D") + "|" + Fixture.Calls + ":" + Fixture.PostInfo +
                              "|" + Fixture.Captured + "|" + Fixture.Wire + "|" +
                              Fixture.H(Fixture.Log.Trace));
        }
    }
}

using System;
using System.Text;
using System.IO;
using System.Collections.Generic;
using OpenMetaverse;
using OpenSim.Services.Interfaces;
using OpenSim.Framework;
using OpenSim.Server.Base;
namespace OpenSim.Framework
{
    public static class Util
    {
        public static int UnixTimeSinceEpoch() => 123;
        public static readonly Encoding UTF8NoBomEncoding = new UTF8Encoding(false);
    }
}
public class IOSHttpRequest
{
}
public class IOSHttpResponse
{
}
public class Logger
{
    public void DebugFormat(string s, params object[] args)
    {
    }
    public void ErrorFormat(string s, params object[] args)
    {
    }
}
public partial class AccountHandler
{
    IUserAccountService m_UserAccountService;
    bool m_AllowCreateUser, m_AllowSetAccount;
    Logger m_log = new Logger();
    static byte[] ResultFailureBytes = Encoding.UTF8.GetBytes(
        "<?xml version=\"1.0\"?><ServerResponse><result>Failure</result></ServerResponse>");
    public static void Main(string[] args)
    {
        Culture.SetCurrentCulture();
        string config = File.ReadAllText(args[0]);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var service = new UserAccountService();
            var handler = new AccountHandler { m_UserAccountService = service,
                                               m_AllowCreateUser = config.Contains("AllowCreateUser=true"),
                                               m_AllowSetAccount = config.Contains("AllowSetAccount=true") };
            using var stream = new MemoryStream(Convert.FromHexString(line));
            var response =
                handler.ProcessRequest("/accounts", stream, new IOSHttpRequest(), new IOSHttpResponse());
            Console.WriteLine(Convert.ToHexString(response).ToLowerInvariant() + "|" +
                              Convert.ToHexString(Encoding.UTF8.GetBytes(service.trace)).ToLowerInvariant());
        }
    }
}
public class UserAccountService : IUserAccountService
{
    public string trace = "";
    static UUID seven = new UUID("00000000-0000-0000-0000-000000000007");
    static UserAccount Account(UUID id) => new UserAccount(id) {
        FirstName = "First",
        LastName = "Last",
        Created = 123,
        UserLevel = 7,
        UserFlags = 9,
        ServiceURLs = new Dictionary<string, object> { { "url", "http://host/a;b*c" } }
    };
    public UserAccount GetUserAccount(UUID scope, UUID id)
    {
        trace += "id:" + scope + ":" + id + ";";
        if (id.ToString().EndsWith("9"))
            throw new Exception();
        return id == seven ? Account(id) : null;
    }
    public UserAccount GetUserAccount(UUID scope, string email)
    {
        trace += "email:" + scope + ":" + email + ";";
        return email == "mail" ? Account(seven) : null;
    }
    public UserAccount GetUserAccount(UUID scope, string first, string last)
    {
        trace += "name:" + scope + ":" + first + ":" + last + ";";
        return first == "First" && last == "Last" ? Account(seven) : null;
    }
    public List<UserAccount> GetUserAccounts(UUID scope, string query)
    {
        trace += "search:" + scope + ":" + query + ";";
        if (query == "throw")
            throw new Exception();
        if (query == "empty")
            return null;
        var r = new List<UserAccount>();
        if (query == "nullitem")
            r.Add(null);
        r.Add(Account(seven));
        if (query == "two")
            r.Add(Account(new UUID("00000000-0000-0000-0000-000000000008")));
        return r;
    }
    public List<UserAccount> GetUserAccounts(UUID scope, List<string> ids)
    {
        trace += "multi:" + scope + ":";
        var r = new List<UserAccount>();
        foreach (var raw in ids)
        {
            trace += raw + ",";
            UUID.TryParse(raw, out UUID id);
            r.Add(id == seven ? Account(seven) : null);
        }
        trace += ';';
        return r;
    }
    public List<UserAccount> GetUserAccountsWhere(UUID scope,string where)=>null;
    public void InvalidateCache(UUID id)
    {
    }
    public bool StoreUserAccount(UserAccount a)
    {
        trace += "store:" + (a.FirstName ?? "_") + ":" + (a.LastName ?? "_") + ":" + (a.Email ?? "_") + ":" +
                 a.Created + ":" + a.UserLevel + ":" + a.UserFlags + ":" + (a.UserTitle ?? "_") + ";";
        if (a.UserLevel == -98)
            throw new Exception();
        return a.UserLevel != -99;
    }
    public UserAccount CreateUser(UUID scope, UUID id, string first, string last, string password,
                                  string email, string model)
    {
        trace += "create:" + scope + ":" + id + ":" + first + ":" + last + ":" + password + ":" + email +
                 ":" + model + ";";
        if (password == "throw")
            throw new Exception();
        return password == "fail" ? null : new UserAccount(scope, id, first, last, email);
    }
}

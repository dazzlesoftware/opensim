using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Globalization;
using System.Threading;
using OpenMetaverse;
using OpenSim.Framework;

namespace OpenSim.Framework
{
    public partial interface IClientAPI
    {
        bool IsActive { get; }

        void SendAvatarProperties(UUID id, string about, string born, byte[] membership, string first, uint flags, UUID firstImage, UUID image, string url, UUID partner);
        void SendAvatarInterestsReply(UUID id, uint wants, string want, uint skills, string skill, string language);
        void SendAvatarGroupsReply(UUID id, GroupMembershipData[] groups);
    }
}

partial class Client
{
    public virtual bool IsActive => throw new Exception("unexpected active");

    public virtual void SendAvatarProperties(UUID id, string about, string born, byte[] membership, string first, uint flags, UUID firstImage, UUID image, string url, UUID partner) => throw new Exception("unexpected properties");
    public virtual void SendAvatarInterestsReply(UUID id, uint wants, string want, uint skills, string skill, string language) => throw new Exception("unexpected interests");
    public virtual void SendAvatarGroupsReply(UUID id, GroupMembershipData[] groups) => throw new Exception("unexpected groups");
}

namespace Services.Interfaces
{
    public class PresenceInfo
    {
    }
}

class AsyncAccount
{
    public int UserFlags, Created;
    public string UserTitle;
}

class AsyncAccounts
{
    public AsyncAccount GetUserAccount(UUID scope, UUID id)
    {
        F.Trace += "account:" + scope + ":" + id + ";";
        AsyncHost.Fail(3, "account");
        return AsyncHost.X(2) ? null : AsyncHost.Account;
    }
}

class AsyncAgent
{
    public AsyncAgent(string url)
    {
        F.Trace += "connector:" + F.T(url) + ";";
        AsyncHost.Fail(5, "connector");
    }

    public Dictionary<string, object> GetUserInfo(UUID id)
    {
        F.Trace += "userinfo:" + id + ";";
        AsyncHost.Fail(6, "userinfo");
        if (AsyncHost.X(7))
            return null;
        var info = new Dictionary<string, object>();
        if (AsyncHost.X(8))
            return info;
        if (AsyncHost.InfoKind >= 8)
        {
            if (AsyncHost.InfoKind == 20)
                throw new Exception("bridge");
            if (AsyncHost.InfoKind == 19)
                return info;
            object flags = AsyncHost.InfoKind switch
            {
                9 => "1",
                10 => null,
                11 => 1L,
                12 => true,
                13 => 1.5,
                14 => new Hashtable(),
                15 => new ArrayList(),
                16 => Array.Empty<byte>(),
                17 => new DateTime(0),
                _ => (object)AsyncHost.Account.UserFlags
            };
            if (flags != null)
                info["user_flags"] = flags;
            info["user_created"] = AsyncHost.InfoKind == 18 ? "1" : AsyncHost.Account.Created;
            return info;
        }

        if (AsyncHost.InfoKind != 6)
            info["user_flags"] = AsyncHost.InfoKind switch
            {
                1 => "1",
                2 => null,
                3 => 1L,
                4 => true,
                5 => 1.5,
                _ => (object)AsyncHost.Account.UserFlags
            };
        info["user_created"] = AsyncHost.InfoKind == 7 ? "1" : AsyncHost.Account.Created;
        return info;
    }
}

class AsyncFriends
{
    public bool IsFriendOnline(UUID owner, UUID id)
    {
        F.Trace += "friends:" + owner + ":" + id + ";";
        AsyncHost.Fail(20, "friends");
        return AsyncHost.X(19);
    }
}

class AsyncPresenceService
{
    public Services.Interfaces.PresenceInfo[] GetAgents(string[] ids)
    {
        F.Trace += "agents:" + string.Join(',', ids) + ";";
        AsyncHost.Fail(27, "agents");
        return AsyncHost.X(25) ? null : AsyncHost.X(26) ? Array.Empty<Services.Interfaces.PresenceInfo>() : new[]
        {
            new Services.Interfaces.PresenceInfo()
        };
    }
}

class AsyncGroups
{
    public GroupMembershipData[] GetMembershipData(UUID id)
    {
        F.Trace += "members:" + id + ";";
        AsyncHost.Fail(16, "members");
        return AsyncHost.X(15) ? null : AsyncHost.Groups;
    }
}

class AsyncRegion
{
    public UUID ScopeID
    {
        get
        {
            F.Trace += "scope;";
            return F.Id(8);
        }
    }
}

interface AsyncNpc
{
    string profileAbout { get; }

    string Born { get; }

    UUID profileImage { get; }
}

class NpcView : AsyncNpc
{
    public string profileAbout
    {
        get
        {
            F.Trace += "npcabout;";
            AsyncHost.Fail(36, "npcabout");
            return F.Text;
        }
    }

    public string Born
    {
        get
        {
            F.Trace += "npcborn;";
            AsyncHost.Fail(37, "npcborn");
            return "npc born";
        }
    }

    public UUID profileImage
    {
        get
        {
            F.Trace += "npcimage;";
            AsyncHost.Fail(38, "npcimage");
            return F.Id(9);
        }
    }
}

class AsyncPresence : Presence
{
    public override object ControllingClient
    {
        get
        {
            F.Trace += "npc;";
            AsyncHost.Fail(35, "npc");
            return AsyncHost.X(34) ? null : new NpcView();
        }
    }

    public override bool IsViewerUIGod
    {
        get
        {
            F.Trace += "god;";
            AsyncHost.Fail(23, "god");
            return AsyncHost.X(22);
        }
    }
}

partial class World
{
    partial void TransformPresence(ref Presence value)
    {
        value = new AsyncPresence
        {
            IsChildAgent = value.IsChildAgent,
            IsNPC = value.IsNPC,
            IsDeleted = AsyncHost.X(33),
            currentParcelUUID = value.currentParcelUUID
        };
    }

    public AsyncAccounts UserAccountService
    {
        get
        {
            F.Trace += "accounts;";
            return AsyncHost.X(1) ? null : new AsyncAccounts();
        }
    }

    public AsyncRegion RegionInfo => AsyncHost.X(4) ? null : new AsyncRegion();
    public AsyncPresenceService PresenceService => AsyncHost.X(24) ? null : new AsyncPresenceService();

    public T RequestModuleInterface<T>()
        where T : class => AsyncHost.X(18) ? null : new AsyncFriends() as T;
}

partial class Users
{
    public void UserWebFailed(UUID id)
    {
        F.Trace += "webfailed:" + id + ";";
        AsyncHost.Fail(13, "webfailed");
    }
}

partial class Log
{
    public void Debug(string text)
    {
        F.Trace += "debug:" + F.T(text) + ":-;";
        AsyncHost.Fail(29, "debug");
    }

    public void Debug(string text, Exception error)
    {
        F.Trace += "debug:" + F.T(text) + ":" + F.T(error.Message) + ";";
        AsyncHost.Fail(29, "debug");
    }

    public void Error(string value)
    {
        int split = value.LastIndexOf(" : ", StringComparison.Ordinal);
        string message = value.Substring("[UserProfileModule]: Process fail ".Length, split - "[UserProfileModule]: Process fail ".Length);
        F.Trace += "error:" + F.T(message) + ";";
        AsyncHost.Fail(28, "processerror");
    }
}

class AsyncLegacy
{
    readonly string uri;
    public AsyncLegacy(string uri)
    {
        this.uri = uri;
    }

    public bool RequestAvatarPropertiesUsingOpenProfile(ref UserProfileProperties props)
    {
        F.Trace += "legacy:" + AsyncHost.Dump(props) + ";";
        AsyncHost.Fail(10, "legacy");
        if (AsyncHost.X(52))
            return new OpenSim.Services.UserProfilesService.OpenProfileClient(uri).RequestAvatarPropertiesUsingOpenProfile(ref props);
        if (AsyncHost.X(11))
            props = AsyncHost.Fill(F.Id(4));
        if (AsyncHost.X(12))
            props = null;
        if (props != null)
            props.AboutText = "legacy";
        return AsyncHost.X(9);
    }
}

partial class Rpc
{
    partial void Props(ref object data, string method, string uri, string id, ref bool? handled)
    {
        if (method is "avatar_properties_request" or "avatar_properties_update" or "avatar_interests_update")
            handled = AsyncHost.Rpc(ref data, method, uri, id);
    }
}

partial class Original
{
    AsyncGroups m_groupsModule;
    bool m_allowUserProfileWebURLs = true;
    readonly ConcurrentStack<AsyncPropsRequest> m_asyncLocalRequests = new(), m_asyncHGRequests = new();
    readonly object m_asyncLocalRequestsLock = new(), m_asyncHGRequestsLock = new();
    bool m_asyncLocalRequestsRunning, m_asyncHGRequestsRunning;
    public void Configure()
    {
        m_groupsModule = AsyncHost.X(14) ? null : new AsyncGroups();
        m_allowUserProfileWebURLs = !AsyncHost.X(0);
    }

    public object QueueLock(bool hg) => hg ? m_asyncHGRequestsLock : m_asyncLocalRequestsLock;
    public string QueueState()
    {
        string One(ConcurrentStack<AsyncPropsRequest> q)
        {
            var rows = new List<string>();
            foreach (var r in q.ToArray())
                rows.Add(r.agent + ":" + AsyncHost.ClientName(r.client) + ":" + PicksFixture.Identity(r.presence) + ":" + r.reqtype);
            return string.Join(',', rows);
        }

        return (m_asyncLocalRequestsRunning ? 1 : 0) + ":" + One(m_asyncLocalRequests) + ";" + (m_asyncHGRequestsRunning ? 1 : 0) + ":" + One(m_asyncHGRequests);
    }

    public void Direct(IClientAPI client, int type) => ProcessRequest(new AsyncPropsRequest { client = client, agent = F.Id(3), reqtype = type });
    public bool AccountData() => GetUserAccountData(F.Id(3), out AsyncHost.LastAccount);
    public bool ProfileData() => GetProfileData(ref AsyncHost.Input, (F.Flags & 16) != 0, F.Uri, out AsyncHost.Message);
}

class AsyncModule : Original
{
    public override bool IsFriendOnline(IClientAPI client, UUID id)
    {
        F.Trace += "online:" + AsyncHost.ClientName(client) + ":" + id + ";";
        return !AsyncHost.X(51);
    }

    public bool BaseOnline(IClientAPI client) => base.IsFriendOnline(client, F.Id(3));
}

class AsyncClient : Client
{
    public int Id;
    bool callback;
    public AsyncClient(int id)
    {
        Id = id;
    }

    public override UUID AgentId
    {
        get
        {
            if (Id == 1)
                return base.AgentId;
            F.Trace += "agent:" + F.Id(Id) + ";";
            F.Fail(8192, "agent");
            return F.Id(Id);
        }
    }

    public override bool IsActive
    {
        get
        {
            F.Trace += "active:" + Id + ";";
            AsyncHost.Fail(39, "active");
            return AsyncHost.Active[Id];
        }
    }

    public override ProfileFixtureAgent SceneAgent => AsyncHost.X(21) ? null : new AsyncPresence();

    public override void SendAvatarProperties(UUID id, string about, string born, byte[] membership, string first, uint flags, UUID firstImage, UUID image, string url, UUID partner)
    {
        string member = PicksFixture.Identity(membership);
        F.Trace += "props:" + Id + ":" + id + ":" + F.T(about) + ":" + F.T(born) + ":" + member + ":" + (membership == null ? "N" : Convert.ToHexString(membership).ToLowerInvariant()) + ":" + F.T(first) + ":" + flags + ":" + firstImage + ":" + image + ":" + F.T(url) + ":" + partner + ";";
        AsyncHost.Fail(40, "props");
        if (AsyncHost.X(43) && AsyncHost.Current.Cache.TryGetValue(id, out var e) && e?.props != null)
        {
            e.props.WantToMask = 99;
            e.props.WebUrl = "callback";
        }

        if (AsyncHost.X(44))
            AsyncHost.Active[2] = false;
        if (AsyncHost.X(47))
            AsyncHost.Current.Cache.Clear();
        if (AsyncHost.X(46) && !callback)
        {
            callback = true;
            try
            {
                AsyncHost.Current.RequestAvatarProperties(this, id);
            }
            finally
            {
                callback = false;
            }
        }
    }

    public override void SendAvatarInterestsReply(UUID id, uint wants, string want, uint skills, string skill, string language)
    {
        F.Trace += "interests:" + Id + ":" + id + ":" + wants + ":" + F.T(want) + ":" + skills + ":" + F.T(skill) + ":" + F.T(language) + ";";
        AsyncHost.Fail(41, "interests");
        if (AsyncHost.X(45) && AsyncHost.Groups.Length > 0 && AsyncHost.Groups[0] != null)
            AsyncHost.Groups[0].GroupName = "mutated group";
    }

    public override void SendAvatarGroupsReply(UUID id, GroupMembershipData[] groups)
    {
        foreach (var g in groups)
            if (g == null)
                throw new NullReferenceException();
        F.Trace += "groups:" + Id + ":" + id + ":" + PicksFixture.Identity(groups) + ":" + AsyncHost.GroupRows(groups) + ";";
        AsyncHost.Fail(42, "groups");
    }
}

static class AsyncHost
{
    public static ulong Extra;
    public static int InfoKind;
    public static AsyncAccount Account, LastAccount;
    public static AsyncModule Current;
    public static UserProfileProperties Input;
    public static object Last;
    public static string Message;
    public static GroupMembershipData[] Groups;
    public static AsyncClient[] Clients;
    public static bool[] Active;
    public static List<Action> Jobs;
    static Thread locker;
    static ManualResetEventSlim entered, released;
    public static void Release()
    {
        if (locker == null)
            return;
        released.Set();
        locker.Join();
        locker = null;
        entered.Dispose();
        released.Dispose();
    }

    public static void Hold(bool hg)
    {
        Release();
        entered = new(false);
        released = new(false);
        object gate = Current.QueueLock(hg);
        locker = new Thread(() =>
        {
            lock (gate)
            {
                entered.Set();
                released.Wait();
            }
        });
        locker.IsBackground = true;
        locker.Start();
        entered.Wait();
    }

    public static bool X(int bit) => (Extra & (1UL << bit)) != 0;
    public static void Fail(int bit, string message)
    {
        if (X(bit))
            throw new Exception(message);
    }

    public static string ClientName(IClientAPI client) => client == null ? "0" : ((AsyncClient)client).Id.ToString();
    public static void Schedule(Action work)
    {
        F.Trace += "schedule;";
        Fail(30, "schedule");
        if (X(31))
            work();
        else
            Jobs.Add(work);
    }

    public static UserProfileProperties Fill(UUID id) => new UserProfileProperties
    {
        UserId = id,
        PartnerId = F.Id(7),
        PublishProfile = true,
        PublishMature = true,
        WebUrl = F.Text,
        WantToMask = int.MinValue,
        WantToText = F.Text,
        SkillsMask = int.MaxValue,
        SkillsText = F.Text,
        Language = F.Text,
        ImageId = F.Id(9),
        AboutText = F.Text,
        FirstLifeImageId = F.Id(8),
        FirstLifeText = F.Text
    };
    public static string Dump(object data)
    {
        if (data == null)
            return "N";
        if (data is not UserProfileProperties p)
            return "X";
        return p.UserId + ":" + p.PartnerId + ":" + (p.PublishProfile ? 1 : 0) + ":" + (p.PublishMature ? 1 : 0) + ":" + F.T(p.WebUrl) + ":" + p.WantToMask + ":" + F.T(p.WantToText) + ":" + p.SkillsMask + ":" + F.T(p.SkillsText) + ":" + F.T(p.Language) + ":" + p.ImageId + ":" + F.T(p.AboutText) + ":" + p.FirstLifeImageId + ":" + F.T(p.FirstLifeText);
    }

    public static string GroupRows(GroupMembershipData[] groups)
    {
        if (groups == null)
            return "N";
        var rows = new List<string>();
        foreach (var g in groups)
            rows.Add(g == null ? "N" : PicksFixture.Group(g));
        return string.Join(',', rows);
    }

    public static bool Rpc(ref object data, string method, string uri, string id)
    {
        F.Trace += "props-rpc:" + method + ":" + F.T(uri) + ":" + Dump(data) + ":" + (UUID.TryParse(id, out var g) && !g.IsZero() ? 1 : 0) + ";";
        Last = data;
        if (F.Mode == 7)
            throw new Exception("rpc");
        if (F.Mode == 0)
            return false;
        if (F.Mode != 8)
        {
            var old = (UserProfileProperties)data;
            var p = Fill(old.UserId);
            if (F.Mode == 3 || F.Mode == 4)
                p.UserId = F.Id(4);
            if (F.Mode == 10)
                p.UserId = UUID.Zero;
            if (F.Mode == 11)
                p.ImageId = UUID.Zero;
            if (F.Mode == 12)
                p.FirstLifeImageId = UUID.Zero;
            if (F.Mode == 3 || F.Mode == 4)
                data = p;
            else
                foreach (var field in typeof(UserProfileProperties).GetFields())
                    field.SetValue(data, field.GetValue(p));
        }

        if (F.Mode == 5 || F.Mode == 9)
            data = null;
        if (F.Mode == 6)
            data = new UserPreferences();
        Last = data;
        return F.Mode != 2 && F.Mode != 4 && F.Mode != 9;
    }

    public static string State()
    {
        string One(int n)
        {
            Current.Cache.TryGetValue(F.Id(n), out var e);
            if (e == null)
                return "N";
            string identity = PicksFixture.Identity(e), props = PicksFixture.Identity(e.props), member = PicksFixture.Identity(e.membershipType), wait = PicksFixture.Identity(e.ClientsWaitingProps), groups = PicksFixture.Identity(e.avatarGroups);
            var clients = new List<string>();
            if (e.ClientsWaitingProps != null)
                foreach (var c in e.ClientsWaitingProps)
                    clients.Add(ClientName(c));
            return identity + ":" + props + ":" + Dump(e.props) + ":" + F.T(e.born) + ":" + member + ":" + (e.membershipType == null ? "N" : Convert.ToHexString(e.membershipType).ToLowerInvariant()) + ":" + e.flags + ":" + wait + ":" + string.Join(',', clients) + ":" + groups + ":" + GroupRows(e.avatarGroups) + ":" + PicksFixture.Dump(e.picksList);
        }

        var rows = new List<string>();
        for (int i = 0; i < 5; i++)
            rows.Add(i + "=" + One(i));
        string queue = Current.QueueState(), input = PicksFixture.Identity(Input), last = PicksFixture.Identity(Last), account = PicksFixture.Identity(LastAccount);
        return Current.Cache.Count + ":" + string.Join(';', rows) + "|" + queue + "|" + Jobs.Count + "|" + input + ":" + Dump(Input) + "|" + last + ":" + Dump(Last) + "|" + F.T(Message) + "|" + account + ":" + (LastAccount == null ? "N" : LastAccount.UserFlags + ":" + LastAccount.Created + ":" + F.T(LastAccount.UserTitle));
    }

    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            F.Mode = int.Parse(p[0]);
            F.Flags = int.Parse(p[1]);
            Extra = ulong.Parse(p[2]);
            Account = new AsyncAccount
            {
                UserFlags = int.Parse(p[3]),
                Created = int.Parse(p[4]),
                UserTitle = PicksFixture.Decode(p[5])
            };
            F.Uri = PicksFixture.Decode(p[6]);
            F.Text = PicksFixture.Decode(p[7]);
            InfoKind = int.Parse(p[8]);
            F.AssetUri = F.Uri;
            F.Trace = "";
            PicksFixture.Reset();
            Active = new[]
            {
                false,
                true,
                true,
                true,
                true
            };
            Clients = new AsyncClient[]
            {
                null,
                new(1),
                new(2),
                new(3),
                new(4)
            };
            Jobs = new();
            Groups = X(17) ? new GroupMembershipData[]
            {
                null
            }

            : new[]
            {
                new GroupMembershipData
                {
                    GroupID = F.Id(7),
                    GroupName = F.Text,
                    GroupTitle = F.Text,
                    GroupPowers = ulong.MaxValue,
                    ListInProfile = true
                }
            };
            Last = null;
            LastAccount = null;
            Input = Fill(F.Id(1));
            Message = "initial";
            Current = new AsyncModule();
            PicksFixture.Current = Current;
            Current.Configure();
            var output = new List<string>();
            foreach (var token in p[9..])
            {
                string result = "OK";
                try
                {
                    if (token[0] == 't')
                        PicksFixture.Now = double.Parse(token[1..], CultureInfo.InvariantCulture);
                    else if (token.Length == 2 && char.IsDigit(token[1]))
                    {
                        int id = token[1] - '0';
                        var client = Clients[id];
                        switch (token[0])
                        {
                            case 'R':
                                Current.RequestAvatarProperties(client, F.Id(3));
                                break;
                            case 'T':
                                Current.RequestAvatarProperties(client, F.Id(4));
                                break;
                            case 'O':
                                Current.RequestAvatarProperties(client, F.Id(1));
                                break;
                            case 'Z':
                                Current.RequestAvatarProperties(client, UUID.Zero);
                                break;
                            case 'S':
                                Current.RequestAvatarProperties(client, Constants.m_MrOpenSimID);
                                break;
                            case 'D':
                                Current.Direct(client, 0);
                                break;
                            case 'J':
                                Current.Direct(client, 1);
                                break;
                            case 'I':
                                Active[id] = false;
                                break;
                            case 'V':
                                Active[id] = true;
                                break;
                            case 'U':
                                Current.AvatarInterestsUpdate(client, uint.MaxValue, F.Text, 0x80000000, F.Text, F.Text);
                                break;
                            case 'P':
                                Current.AvatarPropertiesUpdate(client, Input);
                                break;
                            case 'F':
                                result = Current.BaseOnline(client) ? "1" : "0";
                                break;
                        }
                    }
                    else
                        switch (token)
                        {
                            case "L":
                                Hold(false);
                                break;
                            case "Y":
                                Hold(true);
                                break;
                            case "l":
                                Release();
                                break;
                            case "W":
                                if (Jobs.Count > 0)
                                {
                                    var job = Jobs[0];
                                    Jobs.RemoveAt(0);
                                    job();
                                }

                                break;
                            case "A":
                                for (int i = 0; Jobs.Count > 0 && i < 32; i++)
                                {
                                    var job = Jobs[0];
                                    Jobs.RemoveAt(0);
                                    job();
                                }

                                break;
                            case "C":
                                Current.Cache.Clear();
                                break;
                            case "K":
                                Input = null;
                                break;
                            case "M":
                                Input = Fill(F.Id(1));
                                break;
                            case "G":
                                result = Current.ProfileData() ? "1" : "0";
                                break;
                            case "H":
                                result = Current.AccountData() ? "1" : "0";
                                break;
                            case "Q":
                                Current.Cache.GetType().GetMethod("Purge", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(Current.Cache, new object[] { null });
                                break;
                            case "N":
                                Current.Cache.AddOrUpdate(F.Id(3), null, 300.0);
                                break;
                            default:
                                var e = new UserProfileCacheEntry
                                {
                                    flags = 39,
                                    born = "seed",
                                    membershipType = new byte[]
                                    {
                                        1,
                                        2,
                                        0
                                    },
                                    picksList = new Dictionary<UUID, string>
                                    {
                                        {
                                            F.Id(7),
                                            "other"
                                        }
                                    }
                                };
                                if (token == "E")
                                {
                                    e.props = Fill(F.Id(3));
                                    e.avatarGroups = Groups;
                                }

                                if (token == "B")
                                    e.ClientsWaitingProps = new()
                                    {
                                        Clients[2],
                                        Clients[1]
                                    };
                                Current.Cache.AddOrUpdate(F.Id(3), e, 300.0);
                                break;
                        }
                }
                catch (Exception error)
                {
                    if (error is System.Reflection.TargetInvocationException && error.InnerException != null)
                        error = error.InnerException;
                    result = "EX:" + F.H(error.Message);
                }

                output.Add(result + ":" + State());
            }

            Console.WriteLine(string.Join('/', output) + "#" + F.H(F.Trace));
            Release();
            Jobs.Clear();
        }
    }
}

static class XMLRPCRequester
{
    public static Hashtable SendRequest(Hashtable request, string method, string uri)
    {
        F.Trace += "legacyrpc:" + request["avatar_id"] + ":" + F.T(uri) + ";";
        if (!AsyncHost.X(9))
            return null;
        return new Hashtable
        {
            {
                "data",
                new ArrayList
                {
                    new Hashtable
                    {
                        {
                            "ProfileUrl",
                            F.Text
                        },
                        {
                            "AboutText",
                            F.Text
                        },
                        {
                            "Image",
                            AsyncHost.X(54) ? F.Text : F.Id(9).ToString()
                        },
                        {
                            "FirstLifeImage",
                            F.Id(8).ToString()
                        },
                        {
                            "wantmask",
                            AsyncHost.X(53) ? F.Text : "-12"
                        },
                        {
                            "wanttext",
                            F.Text
                        },
                        {
                            "skillsmask",
                            "17"
                        },
                        {
                            "languages",
                            F.Text
                        }
                    }
                }
            }
        };
    }
}

/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
using System;
using System.Collections.Generic;
using System.Reflection;
using OpenMetaverse;
using OpenSim.Data;
using OpenSim.Data.Null;
using OpenSim.Services.PresenceService;
using OpenSim.Services.Interfaces;
static class Program
{
    public static double Now;
    public static NullPresenceData Database;
    static void Main(string[] args)
    {
        int seed = int.Parse(args[0]);
        Now = 0;
        Database = new NullPresenceData("", "");
        var second = new NullPresenceData("", "");
        var config = new Nini.Config.IniConfigSource();
        var section = config.AddConfig("PresenceService");
        section.Set("StorageProvider", "fixture");
        section.Set("AllowDuplicatePresences", seed % 2 != 0);
        var service = new PresenceService(config);
        UUID Id(int value) =>
            new UUID("00000000-0000-0000-0000-" + ((seed + 1) * 100 + value).ToString("x12"));
        void Purge()
        {
            foreach (var name in new[] { "BySessionCache", "ByUserCache" })
            {
                var cache = typeof(PresenceService)
                                .GetField(name, BindingFlags.NonPublic | BindingFlags.Static)
                                .GetValue(null);
                cache.GetType()
                    .GetMethod("Purge", BindingFlags.NonPublic | BindingFlags.Instance)
                    .Invoke(cache, new object[] { null });
            }
        }
        var user = "user-" + seed;
        var s1 = Id(1);
        var s2 = Id(2);
        var s3 = Id(3);
        var s4 = Id(4);
        var s5 = Id(5);
        var r1 = Id(11);
        var r2 = Id(12);
        var rows = new List<string>();
        void Flag(bool value) => rows.Add(value ? "1" : "0");
        void Info(PresenceInfo value) => rows.Add(value == null ? "-" : value.UserID + "@" + value.RegionID);
        Flag(service.LoginAgent(user, s1, Id(21)));
        Flag(service.ReportAgent(s1, r1));
        Info(service.GetAgent(s1));
        Flag(service.LoginAgent(user, s2, Id(22)));
        Info(service.GetAgent(s1));
        var agents = service.GetAgents(new[] { user, "missing", user });
        rows.Add(agents.Length.ToString());
        foreach (var agent in agents)
            Info(agent);
        Flag(Database.Delete("SessionID", s2.ToString()));
        Info(service.GetAgent(s2));
        Flag(service.ReportAgent(s2, r2));
        Info(service.GetAgent(s2));
        foreach (var agent in service.GetAgents(new[] { user }))
            Info(agent);
        Flag(service.LoginAgent(user + "-two", s3, Id(23)));
        Flag(service.ReportAgent(s3, UUID.Zero));
        Flag(service.ReportAgent(s3, r1));
        rows.Add(Database.Get("SecureSessionID", Id(23).ToString()).Length.ToString());
        Flag(Database.VerifyAgent(Id(31), Id(23)));
        Flag(service.LogoutRegionAgents(r1));
        Info(service.GetAgent(s1));
        Info(service.GetAgent(s3));
        Flag(service.LoginAgent(user + "-three", s4, Id(24)));
        Database.Delete("SessionID", s4.ToString());
        Now = 300000 + seed % 4;
        Purge();
        Info(service.GetAgent(s4));
        Now = 600001 + seed % 4;
        Purge();
        Info(service.GetAgent(s4));
        Now = seed >= 8 ? 2147483648.0 : 2147483000.0;
        Flag(service.LoginAgent(user + "-wrap", s5, Id(25)));
        var alias = Database.Get(s5);
        Flag(ReferenceEquals(second.Get(s5), alias));
        alias.Data["custom"] = "tag";
        alias.SessionID = Id(6);
        Flag(ReferenceEquals(Database.Get(s5), alias));
        rows.Add(Database.Get("SessionID", s5.ToString()).Length.ToString());
        rows.Add(Database.Get("SessionID", Id(6).ToString()).Length.ToString());
        rows.Add(Database.Get("custom", "tag").Length.ToString());
        Database.Delete("SessionID", s5.ToString());
        Now++;
        Purge();
        Info(service.GetAgent(s5));
        Flag(service.LogoutAgent(s5));
        Info(service.GetAgent(s5));
        var first = new PresenceData { UserID = "order", SessionID = Id(40), Data = new() };
        Database.Store(first);
        var next = new PresenceData { UserID = "order", SessionID = Id(41), Data = new() };
        Database.Store(next);
        Database.Delete("SessionID", first.SessionID.ToString());
        var last = new PresenceData { UserID = "order", SessionID = Id(42), Data = new() };
        Database.Store(last);
        Flag(ReferenceEquals(Database.Get("UserID", "order")[0], last));
        rows.Add(Database.Get("RegionID", "invalid").Length.ToString());
        Flag(!Database.Delete("SessionID", "invalid"));
        Console.WriteLine(string.Join("|", rows));
    }
}

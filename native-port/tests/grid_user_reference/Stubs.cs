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
using System.Linq;
using OpenSim.Data;
namespace log4net
{
    public interface ILog
    {
        void Debug(string value);
        void DebugFormat(string value, params object[] args);
    }
    class Log : ILog
    {
        public void Debug(string value)
        {
        }
        public void DebugFormat(string value, params object[] args)
        {
        }
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type type) => new Log();
    }
}
namespace OpenSim.Framework
{
    public static class Util
    {
        public static readonly DateTime UnixEpoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        public static DateTime ToDateTime(int value) => UnixEpoch.AddSeconds(value);
        public static int UnixTimeSinceEpoch() => Program.Unix;
        public static double GetTimeStampMS() => Program.Now;
    }
}
namespace OpenSim.Framework.Console
{
    public class Commands
    {
        public void AddCommand(string a, bool b, string c, string d, string e, string f,
                               Action<string, string[]> action)
        {
        }
        public void AddCommand(string a, bool b, string c, string d, string e,
                               Action<string, string[]> action)
        {
        }
    }
    public class MainConsole
    {
        public static MainConsole Instance = new();
        public Commands Commands = new();
        public void Output(string value, params object[] args)
        {
        }
    }
    public class ConsoleDisplayList
    {
        public void AddRow(string name, string value)
        {
        }
    }
}
namespace OpenSim.Services.Interfaces
{
    public class GridRegion
    {
    }
}
namespace OpenSim.Services.Base
{
    public class ServiceBase
    {
        public ServiceBase(Nini.Config.IConfigSource config)
        {
        }
        protected T LoadPlugin<T>(string name, object[] args) => (T)(object)Program.Database;
    }
}
public class Database : IGridUserData
{
    readonly List<GridUserData> values = new();
    public bool Allowed = true;
    static GridUserData Copy(GridUserData row) => new() { UserID = row.UserID, Data = new(row.Data) };
    public GridUserData Get(string user) =>
        values.Where(row => row.UserID == user).Select(Copy).FirstOrDefault();
    public GridUserData[] GetAll(string query) =>
        values.Where(row => row.UserID.StartsWith(query, StringComparison.OrdinalIgnoreCase))
            .Select(Copy)
            .ToArray();
    public bool Store(GridUserData row)
    {
        if (!Allowed)
            return false;
        var value = new GridUserData { UserID = row.UserID };
        foreach (var name in new[] { "HomeRegionID", "LastRegionID" })
            value.Data[name] = "00000000-0000-0000-0000-000000000000";
        foreach (var name in new[] { "HomePosition", "HomeLookAt", "LastPosition", "LastLookAt" })
            value.Data[name] = "<0,0,0>";
        value.Data["Online"] = "false";
        value.Data["Login"] = value.Data["Logout"] = "0";
        foreach (var pair in row.Data)
            value.Data[pair.Key] = pair.Value;
        values.RemoveAll(old => old.UserID == row.UserID);
        values.Add(value);
        return true;
    }
}

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
using System.Linq;
using System.Collections.Generic;
using System.Reflection;
using OpenMetaverse;
using OpenSim.Data;
using OpenSim.Services.Interfaces;
using OpenSim.Services.UserAccountService;
static class Program {
    public static double Now;
    public static int Unix;
    public static Database Database;
    static void Main(string[] args){
        Console.InputEncoding=new System.Text.UTF8Encoding(false);Console.OutputEncoding=new System.Text.UTF8Encoding(false);
        if(args[0]=="--vectors"){string line;while((line=Console.ReadLine())!=null){bool parsed=Vector3.TryParse(line,out var value);Console.WriteLine((parsed?1:0)+"|"+value);}return;}
        int seed=int.Parse(args[0]);UUID Id(int number)=>new UUID("00000000-0000-0000-0000-"+((seed+1)*100+number).ToString("x12"));Database=new();Now=0;Unix=1700000000+seed;var config=new Nini.Config.IniConfigSource();config.AddConfig("GridUserService").Set("StorageProvider","fixture");var service=new GridUserService(config);var user=Id(1).ToString();var rows=new List<string>();
        void Flag(bool value)=>rows.Add(value?"1":"0");void Info(GridUserInfo value){if(value==null){rows.Add("-");return;}rows.Add(value.UserID+"~"+value.HomeRegionID+"~"+value.HomePosition+"~"+value.HomeLookAt+"~"+value.LastRegionID+"~"+value.LastPosition+"~"+value.LastLookAt+"~"+value.Online+"~"+(long)(value.Login-OpenSim.Framework.Util.UnixEpoch).TotalSeconds+"~"+(long)(value.Logout-OpenSim.Framework.Util.UnixEpoch).TotalSeconds);}
        Info(service.GetGridUserInfo(user));Flag(service.SetHome(user,Id(2),new(128.25f,-2.5f,70),new(0,1,0)));Info(service.GetGridUserInfo(user));Info(service.LoggedIn(user));Unix++;Flag(service.SetLastPosition(user,Id(99),Id(3),new(1e9f,.1f,-0f),new(1,0,0)));Flag(service.LoggedOut(user,Id(98),Id(4),new(3,4,5),new(0,0,1)));Info(service.GetGridUserInfo(user));foreach(var value in service.GetGridUserInfo(new[]{user,"missing",user}))Info(value);
        var shortUser="short"+seed;Info(service.GetGridUserInfo(shortUser));Info(service.LoggedIn(shortUser));Info(service.GetGridUserInfo(shortUser));Now=300001;var cache=typeof(GridUserService).GetField("cache",BindingFlags.Static|BindingFlags.NonPublic).GetValue(null);cache.GetType().GetMethod("Purge",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(cache,new object[]{null});Info(service.GetGridUserInfo(shortUser));
        var prefix=Id(10).ToString();for(int i=0;i<4;i++){var row=new GridUserData{UserID=prefix+";grid"+i};row.Data["Login"]=i==2&&seed%2!=0?"invalid":(100+i*100).ToString();row.Data["Logout"]="0";row.Data["HomePosition"]=i==1?"prefix<1,2,3>suffix":"bad";Database.Store(row);}Info(service.GetGridUserInfo(prefix+";ignored"));
        Database.Allowed=false;Flag(service.SetHome(user,Id(20),new(6,7,8),new(0,1,0)));Info(service.GetGridUserInfo(user));var phantom=Id(21).ToString();Info(service.LoggedIn(phantom));Info(service.GetGridUserInfo(phantom));Flag(Database.Get(phantom)!=null);Database.Allowed=true;foreach(var pair in Database.Get(user).Data.OrderBy(pair=>pair.Key,StringComparer.Ordinal))rows.Add(pair.Key+"="+pair.Value);Console.WriteLine(string.Join("|",rows));
    }
}

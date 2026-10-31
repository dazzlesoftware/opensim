using System;using System.Collections.Generic;using System.Reflection;using System.Data;using System.Data.SQLite;using OpenMetaverse;using OpenSim.Data;using OpenSim.Framework;using OpenSim.Services.Interfaces;
namespace OpenSim.Framework{public static class Util{public static int UnixTimeSinceEpoch()=>123;}}
public partial class OriginalTable<T> where T:class,new(){
    Dictionary<string,FieldInfo> m_Fields=new Dictionary<string,FieldInfo>();FieldInfo m_DataField;string m_Realm="UserAccounts";SQLiteConnection m_Connection;
    public OriginalTable(SQLiteConnection c){m_Connection=c;foreach(var f in typeof(T).GetFields(BindingFlags.Instance|BindingFlags.Public|BindingFlags.DeclaredOnly)){if(f.Name=="Data")m_DataField=f;else m_Fields[f.Name]=f;}}
    int ExecuteNonQuery(SQLiteCommand cmd,SQLiteConnection c){lock(c){cmd.Connection=c;return cmd.ExecuteNonQuery();}}
    List<string> m_ColumnNames;
    IDataReader ExecuteReader(SQLiteCommand cmd,SQLiteConnection c){cmd.Connection=c;return cmd.ExecuteReader();}
}
public partial class AccountRecordReference{
    OriginalTable<UserAccountData> m_Database;
    public static void Main(){Culture.SetCurrentCulture();for(int mode=0;mode<24;++mode){
        using var c=new SQLiteConnection("Data Source=:memory:");c.Open();using var q=c.CreateCommand();
        q.CommandText="CREATE TABLE UserAccounts (PrincipalID CHAR(36) PRIMARY KEY,ScopeID CHAR(36) NOT NULL,FirstName VARCHAR(64) NOT NULL,LastName VARCHAR(64) NOT NULL,Email VARCHAR(64),ServiceURLs TEXT,Created INT,UserLevel INTEGER NOT NULL DEFAULT 0,UserFlags INTEGER NOT NULL DEFAULT 0,UserTitle VARCHAR(64) NOT NULL DEFAULT '',active BOOLEAN NOT NULL DEFAULT TRUE)";q.ExecuteNonQuery();
        var owner=new AccountRecordReference{m_Database=new OriginalTable<UserAccountData>(c)};
        var a=new UserAccount(UUID.Zero,new UUID("00000000-0000-0000-0000-000000000007"),"First","Last","mail"){UserLevel=-4,UserFlags=17,UserTitle="Title"};
        if((mode&1)!=0)a.Email=null;if((mode&2)!=0)a.UserTitle="";if((mode&4)!=0)a.UserCountry="CA";if((mode&8)!=0)a.FirstName=null;
        if(mode>=16)a.ServiceURLs["null"]=null;else a.ServiceURLs["url"]="https://host/two words/試験";
        string status="OK";try{owner.StoreUserAccount(a);}catch{status="EX";}
        q.CommandText="SELECT * FROM UserAccounts WHERE FirstName='First' AND LastName='Last'";using var reader=q.ExecuteReader();
        Console.Write(mode+"|"+status+"|");if(!reader.Read()){Console.WriteLine("missing");continue;}
        var d=new UserAccountData{PrincipalID=new UUID(reader["PrincipalID"].ToString()),ScopeID=new UUID(reader["ScopeID"].ToString()),FirstName=reader["FirstName"].ToString(),LastName=reader["LastName"].ToString(),Data=new Dictionary<string,string>()};
        foreach(string key in new[]{"Email","ServiceURLs","Created","UserLevel","UserFlags","UserTitle"})d.Data[key]=reader[key].ToString();
        var found=owner.MakeUserAccount(d);Console.WriteLine(found.PrincipalID+"|"+found.Created+"|"+found.UserLevel+"|"+found.UserFlags+"|"+found.Email+"|"+found.UserTitle+"|"+found.ServiceURLs["url"]);
    }
        using var db=new SQLiteConnection("Data Source=:memory:");db.Open();using var schema=db.CreateCommand();schema.CommandText="CREATE TABLE UserAccounts (PrincipalID CHAR(36) PRIMARY KEY,ScopeID CHAR(36),FirstName TEXT,LastName TEXT,Email TEXT,Created INT,UserLevel INT,UserFlags INT,UserTitle TEXT DEFAULT '',ServiceURLs TEXT)";schema.ExecuteNonQuery();
        var fixture=new AccountRecordReference{m_Database=new OriginalTable<UserAccountData>(db)};
        var scopes=new[]{UUID.Zero,new UUID("00000000-0000-0000-0000-000000000001"),new UUID("00000000-0000-0000-0000-000000000002"),new UUID("00000000-0000-0000-0000-000000000003")};
        var ids=new[]{new UUID("00000000-0000-0000-0000-000000000011"),new UUID("00000000-0000-0000-0000-000000000012"),new UUID("00000000-0000-0000-0000-000000000013"),new UUID("00000000-0000-0000-0000-000000000014")};
        for(int i=0;i<3;++i)fixture.StoreUserAccount(new UserAccount(scopes[i],ids[i],"First","Last","shared@example"));
        foreach(var scope in scopes){
            var named=fixture.GetUserAccount(scope,"First","Last");Console.WriteLine("name|"+(named==null?"missing":named.PrincipalID.ToString()));
            foreach(var email in new[]{"shared@example","Shared@example","missing","' OR 1=1 --"}){var a=fixture.GetUserAccount(scope,email);Console.WriteLine("email|"+(a==null?"missing":a.PrincipalID.ToString()));}
            foreach(var id in ids){var a=fixture.GetUserAccount(scope,id);Console.WriteLine("id|"+(a==null?"missing":a.PrincipalID.ToString()));}
            foreach(var query in new[]{"First","Las","Fir xxx","xxx Las","fIr","%%%","___","a b First","a b c First","a b","First Last third","a'bc","\tFirst\t","😀","   ","First  Last"}){
                string result="search|";try{foreach(var a in fixture.GetUserAccounts(scope,query))result+=a.PrincipalID+",";}catch{result+="EX";}Console.WriteLine(result);
            }
        }
    }
}

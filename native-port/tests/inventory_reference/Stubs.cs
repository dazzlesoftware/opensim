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
// Adapter boundaries for compiling the actual XInventoryService source.
// The in-memory provider is a fixture, not an oracle for SQLite provider parity.
using System;
using System.Collections.Generic;
using System.Linq;
using OpenMetaverse;
using OpenSim.Data;
namespace log4net {
    public interface ILog {void WarnFormat(string value,params object[] args);}
    sealed class Log:ILog {public void WarnFormat(string value,params object[] args){}}
    public static class LogManager {public static ILog GetLogger(Type type)=>new Log();}
}
namespace Nini.Config {
    public interface IConfig {string GetString(string key,string fallback);bool GetBoolean(string key,bool fallback);}
    public interface IConfigSource {Configs Configs{get;}}
    public class Configs {public IConfig this[string name]=>new Config();}
    public class Config:IConfig {public string GetString(string key,string fallback)=>key=="StorageProvider"?"fixture":fallback;public bool GetBoolean(string key,bool fallback)=>Program.AllowDelete;}
    public class Source:IConfigSource {public Configs Configs{get;}=new();}
}
namespace OpenSim.Services.Interfaces {public interface IInventoryService {}}
namespace OpenSim.Services.Base {
    public class ServiceBase {public ServiceBase(Nini.Config.IConfigSource config){}protected T LoadPlugin<T>(string name,object[] args)=>(T)(object)Program.Database;}
}
namespace OpenSim.Framework {
    public class osUTF8 {readonly string value;public osUTF8(string text){value=text;}public override string ToString()=>value;}
    public partial class InventoryItemBase {
        public UUID AssetID,Owner,ID,Folder,GroupID;
        public int AssetType,InvType,SalePrice,CreationDate;
        public string Name,Description;
        public uint NextPermissions,CurrentPermissions,BasePermissions,EveryOnePermissions,GroupPermissions,Flags;
        public bool GroupOwned;
        public byte SaleType;
        protected string m_creatorId,m_creatorData=string.Empty;
        // CreatorIdentification itself is extracted unchanged from the source.
    }
}
public class Database:IXInventoryData {
    readonly List<XInventoryFolder> folders=new();readonly List<XInventoryItem> items=new();
    static bool Match<T>(T row,string[] fields,string[] values)=>fields.Select((field,i)=>Convert.ToString(typeof(T).GetField(field).GetValue(row))==values[i]).All(x=>x);
    void Increment(UUID id){var folder=folders.Find(x=>x.folderID==id);if(folder!=null)folder.version++;}
    public XInventoryFolder[] GetFolder(string field,string value)=>GetFolders(new[]{field},new[]{value});
    public XInventoryFolder[] GetFolders(string[] fields,string[] values)=>folders.Where(row=>Match(row,fields,values)).Select(row=>row.Clone()).ToArray();
    public XInventoryItem[] GetItems(string[] fields,string[] values)=>items.Where(row=>Match(row,fields,values)).Select(row=>row.Clone()).ToArray();
    public bool StoreFolder(XInventoryFolder value){if(value.folderName.Length>64)value.folderName=value.folderName[..64];folders.RemoveAll(x=>x.folderID==value.folderID);folders.Add(value.Clone());Increment(value.parentFolderID);return true;}
    public bool StoreItem(XInventoryItem value){if(value.inventoryName.Length>64)value.inventoryName=value.inventoryName[..64];if(value.inventoryDescription.Length>128)value.inventoryDescription=value.inventoryDescription[..128];items.RemoveAll(x=>x.inventoryID==value.inventoryID);items.Add(value.Clone());Increment(value.parentFolderID);return true;}
    public bool DeleteFolders(string field,string value)=>DeleteFolders(new[]{field},new[]{value});
    public bool DeleteFolders(string[] fields,string[] values)=>folders.RemoveAll(row=>Match(row,fields,values))>0;
    public bool DeleteItems(string field,string value)=>DeleteItems(new[]{field},new[]{value});
    public bool DeleteItems(string[] fields,string[] values){var deleted=GetItems(fields,values);items.RemoveAll(row=>Match(row,fields,values));foreach(var parent in deleted.Select(x=>x.parentFolderID).Distinct())Increment(parent);return deleted.Length>0;}
    public bool MoveItem(string id,string parent){var value=items.Find(x=>x.inventoryID.ToString()==id);if(value==null)return false;var previous=value.parentFolderID;value.parentFolderID=new UUID(parent);Increment(previous);Increment(value.parentFolderID);return true;}
    public bool MoveFolder(string id,string parent){var value=folders.Find(x=>x.folderID.ToString()==id);if(value==null)return false;var previous=value.parentFolderID;value.parentFolderID=new UUID(parent);Increment(previous);Increment(value.parentFolderID);return true;}
    public XInventoryItem[] GetActiveGestures(UUID owner)=>items.Where(x=>x.avatarID==owner&&x.assetType==21&&x.flags==1).Select(x=>x.Clone()).ToArray();
    public int GetAssetPermissions(UUID owner,UUID asset)=>items.Where(x=>x.avatarID==owner&&x.assetID==asset).Aggregate(0,(bits,x)=>bits|x.inventoryCurrentPermissions);
}

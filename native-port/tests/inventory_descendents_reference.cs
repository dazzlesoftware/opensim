using System;using System.Collections.Generic;using System.Text;using System.IO;using OpenMetaverse;using OpenMetaverse.StructuredData;
class IOSHttpRequest {public Stream InputStream;}
class IOSHttpResponse {public byte[] RawBuffer=Array.Empty<byte>();public int StatusCode=200;}
class ExpiringKey<T> {public HashSet<T> Keys=new();public bool ContainsKey(T key)=>Keys.Contains(key);public void Add(T key)=>Keys.Add(key);}
class LogStub {public void Debug(string value){}public void Error(string value){}public void Warn(string value){}}
class Util {public static byte[] UTF8NBGetbytes(string value)=>Encoding.UTF8.GetBytes(value);}
class osStringBuilderCache {public static StringBuilder Acquire()=>new();public static string GetStringAndRelease(StringBuilder value)=>value.ToString();}
class LLSDFetchInventoryDescendents {public UUID folder_id,owner_id;public int sort_order;public bool fetch_folders,fetch_items;}
class InventoryCollection {public UUID OwnerID,FolderID;public int Version,Descendents;public List<InventoryFolderBase> Folders=new();public List<InventoryItemBase> Items=new();}
class InventoryFolderBase {public UUID ID,Owner,ParentID;public string Name;public short Type;public ushort Version;}
partial class InventoryItemBase {public UUID Folder,AssetID,ID,CreatorIdAsUuid,Owner,GroupID;public uint CurrentPermissions,GroupPermissions,EveryOnePermissions,NextPermissions,Flags;public bool GroupOwned;public int AssetType,InvType,SalePrice,CreationDate;public byte SaleType;public string Name,Description;}
class InventoryServiceStub {
 public int Mode,Options;public string Trace="";
 public void Append(string value){if(Trace.Length>0)Trace+=';';Trace+=value;}
 public InventoryCollection[] GetMultipleFoldersContent(UUID owner,UUID[] ids){string call="C"+owner;foreach(var id in ids)call+=","+id;Append(call);if(Mode==6)throw new Exception();if(Mode==1)return null;var result=new List<InventoryCollection>();if(Mode==2)return result.ToArray();foreach(var id in ids)result.Add(Mode==3?null:DescendentsReference.Collection(id,Mode));if(Mode==7)result.Add(DescendentsReference.Collection(DescendentsReference.Id(20),0));if(Mode==8&&result.Count>0)result.RemoveAt(result.Count-1);return result.ToArray();}
 public InventoryFolderBase GetFolder(UUID owner,UUID id){Append("F"+owner+","+id);if(Mode==5)return null;return new(){ID=id,Owner=DescendentsReference.Id(101),Version=9};}
 public InventoryItemBase[] GetMultipleItems(UUID owner,UUID[] ids){string call="I"+owner;foreach(var id in ids)call+=","+id;Append(call);if((Options&16)!=0)throw new Exception();if((Options&4)!=0)return null;return new[]{DescendentsReference.Item(700,6),null,DescendentsReference.Item(701,24)};}
}
class InventoryFolderImpl {public UUID Owner;public int Version;public InventoryServiceStub Backend;InventoryCollection value;public InventoryFolderImpl FindFolder(UUID id){Backend.Append("L"+id);if((Backend.Options&32)!=0)throw new Exception();if(id==DescendentsReference.Id(999))return null;return new(){value=DescendentsReference.Collection(id,0),Version=11};}public List<InventoryFolderBase> RequestListOfFolders()=>value.Folders;public List<InventoryItemBase> RequestListOfItems()=>value.Items;}
class LibraryServiceStub {public InventoryFolderImpl LibraryRootFolder;}
partial class DescendentsReference {
 LogStub m_log=new();InventoryServiceStub m_InventoryService=new();LibraryServiceStub m_LibraryService;UUID libOwner;static byte[] EmptyResponse=Util.UTF8NBGetbytes("<llsd><map><key>folders</key><array /></map></llsd>");
 public static UUID Id(int n)=>new UUID("00000000-0000-0000-0000-"+n.ToString().PadLeft(12,'0'));
 public static InventoryItemBase Item(int n,int type)=>new(){ID=Id(n),Owner=Id(100),Folder=Id(1),AssetID=Id(type==24?700:500),AssetType=type,InvType=6,Name="item<&>",Description="description",Flags=uint.MaxValue};
 public static InventoryCollection Collection(UUID key,int mode)=>new(){FolderID=mode==4||mode==5?UUID.Zero:key,OwnerID=Id(100),Version=7,Folders=new(){new(){ID=Id(900),ParentID=key,Name="folder<&>",Type=-1,Version=0}},Items=new(){Item(201,6),Item(202,24)}};
 static void Main(){string command;while((command=Console.ReadLine())!=null){var v=command.Split(' ');int mode=int.Parse(v[0]),options=int.Parse(v[1]);var core=new DescendentsReference();core.m_InventoryService.Mode=mode;core.m_InventoryService.Options=options;if((options&2)==0){core.libOwner=Id(200);core.m_LibraryService=new(){LibraryRootFolder=new(){Owner=Id(200),Backend=core.m_InventoryService}};}var bad=new ExpiringKey<UUID>();if((options&1)!=0)bad.Add(Id(2));var response=new IOSHttpResponse();string result;try{core.FetchInventoryDescendentsRequest(new(){InputStream=new MemoryStream(v[2]=="-"?Array.Empty<byte>():Convert.FromHexString(v[2]))},response,(options&8)!=0?null:bad);result=response.StatusCode+":"+Convert.ToHexString(response.RawBuffer).ToLowerInvariant();}catch(Exception){result="EXCEPTION";}Console.WriteLine(result+"|"+core.m_InventoryService.Trace+"|"+(bad.ContainsKey(Id(1))?"1":"0")+(bad.ContainsKey(Id(2))?"1":"0")+(bad.ContainsKey(Id(3))?"1":"0"));}}
}

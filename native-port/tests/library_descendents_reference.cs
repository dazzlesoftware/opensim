using System;using System.Collections.Generic;using System.Text;using System.IO;using OpenMetaverse;using OpenMetaverse.StructuredData;
class IOSHttpRequest {public Stream InputStream;}
class IOSHttpResponse {public byte[] RawBuffer=Array.Empty<byte>();public int StatusCode=200;}
class ExpiringKey<T>{public HashSet<T> Keys=new();public bool ContainsKey(T key)=>Keys.Contains(key);public void Add(T key)=>Keys.Add(key);}
class LogStub {public void Debug(string s){}public void ErrorFormat(string s,params object[] args){}public void Warn(string s){}}
class Util {public static byte[] UTF8NBGetbytes(string value)=>Encoding.UTF8.GetBytes(value);}
class osStringBuilderCache {public static StringBuilder Acquire()=>new();public static string GetStringAndRelease(StringBuilder value)=>value.ToString();}
class LLSDFetchInventoryDescendents {public UUID folder_id,owner_id;public int sort_order;public bool fetch_folders,fetch_items;}
class InventoryCollection {public UUID OwnerID,FolderID;public int Version,Descendents;public List<InventoryFolderBase> Folders=new();public List<InventoryItemBase> Items=new();}
class InventoryFolderBase {public UUID ID,ParentID;public string Name;public short Type;public ushort Version;}
partial class InventoryItemBase {public UUID Folder,AssetID,ID,CreatorIdAsUuid,Owner,GroupID;public uint CurrentPermissions,GroupPermissions,EveryOnePermissions,NextPermissions,Flags;public bool GroupOwned;public int AssetType,InvType,SalePrice,CreationDate;public byte SaleType;public string Name,Description;}
class SceneStub{}
class LibraryStub {public bool Available=true;public int Mode,Options;public string Trace="";public Node Root=new();public void Add(string s){if(Trace.Length>0)Trace+=';';Trace+=s;}public Node LibraryRootFolder{get{Add("R");return Available?Root:null;}}}
class Node {public UUID Owner;public int Version;public LibraryStub Library;public InventoryCollection Value;
 public Node FindFolder(UUID id){Library.Add("F"+id);if((Library.Mode&8)!=0)throw new Exception();if((Library.Mode&4)!=0||id==LibraryDescReference.Id(999))return null;var value=LibraryDescReference.Collection(id,Library.Options);if((Library.Mode&32)!=0)Library.Available=false;return new(){Value=value,Version=7};}
 public List<InventoryFolderBase> RequestListOfFolders()=>Value.Folders;public List<InventoryItemBase> RequestListOfItems()=>Value.Items;
}
partial class LibraryDescReference {
 LogStub m_log=new();LibraryStub m_LibraryService;UUID libOwner;SceneStub m_Scene;static byte[] EmptyResponse=Util.UTF8NBGetbytes("<llsd><map><key>folders</key><array /></map></llsd>");
 public static UUID Id(int n)=>new UUID("00000000-0000-0000-0000-"+n.ToString().PadLeft(12,'0'));
 public static InventoryCollection Collection(UUID key,int options){var c=new InventoryCollection();if((options&16)==0)c.Folders.Add(new(){ID=Id(900),ParentID=key,Name="library <&>",Type=-1});if((options&32)==0)c.Items.Add(new(){ID=Id(201),Owner=Id(200),Folder=key,AssetID=Id(500),Name="library item",Flags=uint.MaxValue,Description=""});return c;}
 static void Main(){string command;while((command=Console.ReadLine())!=null){var p=command.Split(' ');int mode=int.Parse(p[0]),options=int.Parse(p[1]);var library=new LibraryStub{Available=(mode&2)==0,Mode=mode,Options=options};library.Root.Owner=(options&8)!=0?UUID.Zero:Id(200);library.Root.Library=library;var bad=new ExpiringKey<UUID>();if((options&1)!=0)bad.Add(Id(2));bool constructed=false;string result;try{var core=new LibraryDescReference((mode&64)!=0?null:library,null);constructed=true;if((mode&1)!=0)library.Available=false;if((mode&16)!=0)library.Root.Owner=Id(201);var response=new IOSHttpResponse();core.FetchRequest(new(){InputStream=new MemoryStream(p[2]=="-"?Array.Empty<byte>():Convert.FromHexString(p[2]))},response,(options&2)!=0?null:bad,(options&4)!=0?UUID.Zero:Id(42));result=response.StatusCode+":"+Convert.ToHexString(response.RawBuffer).ToLowerInvariant();}catch(Exception){result=constructed?"EXCEPTION":"CTOR";}Console.WriteLine(result+"|"+library.Trace+"|"+(bad.ContainsKey(Id(1))?"1":"0")+(bad.ContainsKey(Id(2))?"1":"0")+(bad.ContainsKey(Id(3))?"1":"0"));}}
}

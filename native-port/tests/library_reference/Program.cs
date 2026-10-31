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
using OpenMetaverse;
using OpenSim.Services.InventoryService;
static class Program {
    static void Tree(){
        var root=new OpenSim.Framework.InventoryFolderImpl{ID=new UUID("00000000-0000-0000-0000-000000000001"),Owner=UUID.Random()};var a=new UUID("00000000-0000-0000-0000-000000000002");var b=new UUID("00000000-0000-0000-0000-000000000003");var id=new UUID("00000000-0000-0000-0000-000000000004");
        var child=root.CreateChildFolder(a,"A",65535);var nested=child.CreateChildFolder(b,"B",6);var item=new OpenSim.Framework.InventoryItemBase{ID=id,Name="Object",Folder=b,AssetID=a};nested.Items.Add(id,item);var checks=new List<bool>();
        checks.Add(child.Owner==root.Owner&&child.ParentID==root.ID&&child.Type==-1&&child.Version==0);checks.Add(root.CreateChildFolder(a,"Duplicate",1)==null);checks.Add(root.FindItem(id)==item&&root.FindAsset(a)==item&&root.FindFolder(b)==nested);checks.Add(root.FindFolderForType(6)==null&&child.FindFolderForType(6)==nested);
        checks.Add(root.FindFolderByPath("\u2003 A/B \u00a0")==nested);checks.Add(root.FindFolderByPath("A//B")==null&&root.FindFolderByPath("/A")==null);checks.Add(root.FindFolderByPath("A/")==child&&root.FindFolderByPath("/")==root);checks.Add(root.FindItemByPath("A/B/Object")==item&&root.FindItemByPath(" A/B/Object")==null);checks.Add(root.TotalCount==1);
        var snapshot=nested.RequestListOfItems();item.Name="Changed";checks.Add(snapshot[0].Name=="Changed");checks.Add(root.DeleteItem(id)&&!root.DeleteItem(id)&&root.TotalCount==0&&snapshot[0]==item);nested.Items.Add(id,item);checks.Add(root.RemoveChildFolder(a)==child&&!root.ContainsChildFolder(a)&&root.TotalCount==0&&child.ParentID==root.ID);root.AddChildFolder(child);checks.Add(root.TotalCount==1);root.Purge();checks.Add(root.RequestListOfFolderImpls().Count==0&&nested.Items.Count==0&&root.TotalCount==0);Console.WriteLine("T|"+string.Join("|",checks.Select(value=>value?1:0)));
    }
    static void Main(string[] args){
        if(args[0]=="--tree"){Tree();return;}
        if(args[0]=="--login"){
            int seed=int.Parse(args[1]);UUID Id(int number)=>new UUID("00000000-0000-0000-0000-"+number.ToString("x12"));
            List<OpenSim.Framework.InventoryFolderBase> folders=seed==0?null:new();
            if(seed>=2){folders.Add(new(Id(2),"My Inventory",Id(1),8,UUID.Zero,65535));folders.Add(new(Id(3),"My Inventory",Id(1),-1,UUID.Zero,1));folders.Add(new(Id(4),"Nested & folder",Id(1),6,Id(3),7));}
            List<OpenSim.Framework.InventoryItemBase> gestures=seed==2?new(){new(){ID=Id(5),AssetID=Id(6)}}:null;
            LibraryService library=null;if(seed==2){var source=new Nini.Config.IniConfigSource();source.AddConfig("LibraryService").Set("DefaultLibrary",args[2]);library=new LibraryService(source);}
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new LoginFields().Build(folders,library,gestures)));return;
        }
        var config=new Nini.Config.IniConfigSource();var section=config.AddConfig("LibraryService");section.Set("DefaultLibrary",args[0]);var service=new LibraryService(config);var rows=new List<string>();
        foreach(var folder in service.GetAllFolders().Values){
            rows.Add($"F|{folder.ID}|{folder.ParentID}|{folder.Owner}|{folder.Name}|{folder.Type}|{folder.Version}");
            foreach(var item in folder.RequestListOfItems())rows.Add($"I|{item.ID}|{item.Folder}|{item.Owner}|{item.CreatorId}|{item.AssetID}|{item.Name}|{item.Description}|{item.InvType}|{item.AssetType}|{item.BasePermissions}|{item.CurrentPermissions}|{item.NextPermissions}|{item.EveryOnePermissions}|{item.GroupPermissions}|{item.Flags}");
        }
        rows.Sort(StringComparer.Ordinal);foreach(var row in rows)Console.WriteLine(row);
        var ids=service.GetAllFolders().Values.SelectMany(folder=>folder.RequestListOfItems()).Select(item=>item.ID).Distinct().OrderBy(id=>id.ToString(),StringComparer.Ordinal).ToArray();
        foreach(var id in ids){var item=service.GetItem(id);Console.WriteLine($"G|{id}|{item.Name}|{item.Folder}");}if(ids.Length>0){var values=service.GetMultipleItems(new[]{UUID.Zero,ids[0],UUID.Zero,ids[0]});Console.WriteLine($"Q|{values.Length}|{(ReferenceEquals(values[0],values[1])?1:0)}");}
        Console.WriteLine("O|"+string.Join("|",service.GetAllFolders().Keys));var second=new LibraryService(new Nini.Config.IniConfigSource());Console.WriteLine("S|"+(ReferenceEquals(second.LibraryRootFolder,service.LibraryRootFolder)?1:0));Console.WriteLine("M|"+(service.GetMultipleItems(new[]{UUID.Zero})!=null?1:0));
    }
}

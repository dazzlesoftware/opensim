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
using OpenSim.Framework;
using OpenSim.Services.InventoryService;
static class Program {
    public static Database Database;
    public static bool AllowDelete;
    static UUID Id(int value)=>new UUID("00000000-0000-0000-0000-"+value.ToString("x12"));
    static void Main(){string line;while((line=Console.ReadLine())!=null){try{Run(int.Parse(line));}catch(Exception error){Console.WriteLine("ERROR:"+error);}}}
    static void Run(int seed){
        Database=new();AllowDelete=seed%2==0;var service=new XInventoryService(new Nini.Config.Source());var owner=Id(1);var other=Id(2);var observations=new List<string>();
        void Flag(bool value)=>observations.Add(value?"1":"0");
        Flag(service.CreateUserInventory(owner));for(int i=0;i<seed%3;++i)Flag(service.CreateUserInventory(owner));var root=service.GetRootFolder(owner);
        var folder=new InventoryFolderBase(Id(100),"Ordinary",owner,-1,root.ID,(ushort)(seed+1));Flag(service.AddFolder(folder));Flag(service.AddFolder(folder));folder.Name="Edited";folder.Version=0;Flag(service.UpdateFolder(folder));
        var system=service.GetFolderForType(owner,FolderType.Object);system.Name="Override";system.Version=(ushort)(system.Version+seed);Flag(service.UpdateFolder(system));
        var item=new InventoryItemBase{ID=Id(200),Owner=owner,Folder=folder.ID,AssetID=Id(300),AssetType=6,InvType=6,CreatorIdentification=owner+";https://grid.invalid",Name=new string('x',70),Description="Description",CurrentPermissions=0x80004000,SaleType=255,GroupOwned=true};
        Flag(service.AddItem(item));var initial=service.GetItem(other,item.ID);observations.Add(initial.CreatorIdentification);observations.Add(initial.Name.Length.ToString());
        item.Owner=other;item.Folder=root.ID;item.AssetType=21;item.InvType=21;item.CreatorIdentification="different";item.Name="Updated";item.AssetID=Id(301);Flag(service.UpdateItem(item));
        Flag(item.Owner==owner&&item.Folder==folder.ID&&item.AssetType==6&&item.InvType==6);observations.Add(item.CreatorIdentification);Flag(service.GetItem(owner,item.ID).AssetID==Id(301));
        var link=new InventoryItemBase{ID=Id(201),Owner=owner,Folder=folder.ID,AssetID=Id(300),AssetType=24,InvType=6,CreatorIdentification=owner.ToString(),Name="Link",Description=""};Flag(service.AddItem(link));Flag(service.UpdateItem(link));Flag(service.DeleteItems(other,new(){item.ID,link.ID}));Flag(service.GetItem(owner,item.ID)!=null);Flag(service.GetItem(owner,link.ID)!=null);
        var contents=service.GetFolderContent(other,folder.ID);Flag(contents.OwnerID==owner);observations.Add(contents.Items.Count.ToString());
        var skeleton=service.GetInventorySkeleton(owner);var names=skeleton.ToDictionary(x=>x.ID,x=>x.Name);observations.AddRange(skeleton.Select(x=>x.Name+":"+x.Type+":"+x.Version+":"+(x.ParentID==UUID.Zero?"-":names.GetValueOrDefault(x.ParentID,"?"))).OrderBy(x=>x,StringComparer.Ordinal));
        Console.WriteLine(string.Join("|",observations));
    }
}

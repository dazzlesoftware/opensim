using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;
class TaskInventoryItem{public UUID AssetID;public int Type;}
class LiveShape {
 public int Tag;public Primitive.TextureEntry T;public Primitive.RenderMaterials R;
 public Primitive.TextureEntry Textures{get{ProbeLive.Hit(6,Tag);return T;}}
 public UUID SculptTexture{get{ProbeLive.Hit(8,Tag);return Probe.Id(-2);}}
 public UUID ProjectionTextureUUID{get{ProbeLive.Hit(9,Tag);return Probe.Id(-1);}}
 public Primitive.RenderMaterials RenderMaterials{get{ProbeLive.Hit(10,Tag);return R;}}
}
class LiveInventory{public int Tag;public List<TaskInventoryItem> Items;public List<TaskInventoryItem> GetItems(){ProbeLive.Hit(16,Tag);return Items;}}
class LiveAttrs{public int Tag;public OSDMap Store;public bool TryGetStore(string space,string name,out OSDMap result){ProbeLive.Hit(19,Tag);result=Store;return ProbeLive.Profile!=19;}}
class SceneObjectPart {
 public int Tag;public LiveShape Sh;public LiveInventory Inv;public LiveAttrs Attr;public Dictionary<UUID,bool> Anim;public byte[] Particle;
 public LiveShape Shape{get{ProbeLive.Hit(5,Tag);return Sh;}}
 public int GetNumberOfSides(){ProbeLive.Hit(7,Tag);return ProbeLive.Sides;}
 public UUID CollisionSound{get{ProbeLive.Hit(11,Tag);return Probe.Id(11);}}
 public UUID invalidCollisionSoundUUID{get{ProbeLive.Hit(12,Tag);return Probe.Id(99);}}
 public UUID Sound{get{ProbeLive.Hit(13,Tag);return Probe.Id(12);}}
 public byte[] ParticleSystem{get{ProbeLive.Hit(14,Tag);return Particle;}}
 public LiveInventory TaskInventory{get{ProbeLive.Hit(15,Tag);return Inv;}}
 public Dictionary<UUID,bool> Animations{get{ProbeLive.Hit(17,Tag);return Anim;}}
 public LiveAttrs DynAttrs{get{ProbeLive.Hit(18,Tag);return Attr;}}
 public string Name{get{ProbeLive.Hit(20,Tag);return ProbeLive.Profile==32?null:"part";}}
 public UUID UUID{get{ProbeLive.Hit(21,Tag);return Probe.Id(40+Tag);}}
}
class EntityBase{}
class SceneObjectGroup:EntityBase {
 public SceneObjectPart[] List;
 public bool IsDeleted{get{ProbeLive.Hit(1,0);return ProbeLive.Profile==1;}}
 public SceneObjectPart[] Parts{get{ProbeLive.Hit(2,0);return List;}}
 public bool TemporaryInstance{get{ProbeLive.Hit(3,0);return ProbeLive.Profile==29;}}
 public void Dispose(){ProbeLive.Hit(4,0);ProbeLive.Disposed=true;}
 public string Name{get{ProbeLive.Hit(22,0);return ProbeLive.Profile==32?null:"object";}}
 public UUID UUID{get{ProbeLive.Hit(23,0);return Probe.Id(50);}}
}
static class ProbeLive {
 public static bool Active,Disposed;public static int Profile,Sides;static int Mutation,MutationNth,MutationSeen;static SceneObjectPart[] Parts;
 public static void Hit(int point,int tag){if(!Active)return;Probe.Log("L:"+point+":"+tag);Probe.Hook(100+point);if(ProbeScene.Active)ProbeScene.Hit(51,point);if(Mutation!=0&&point==10&&++MutationSeen==MutationNth){if(Mutation==1)Parts[0].Sh.R.overrides[0].data=null;if(Mutation==2)Parts[0].Sh.R=null;if(Mutation==3)Parts[0].Sh=null;}}
 public static void Warn(string s){Probe.Log("W:"+s);Hit(24,0);}
 public static SceneObjectGroup Build(int profile,int sides,int mutation,int nth){
  Profile=profile;Sides=sides;Mutation=mutation;MutationNth=nth;MutationSeen=0;Disposed=false;
  var group=new SceneObjectGroup();Parts=new SceneObjectPart[2];group.List=Parts;
  for(int i=0;i<2;i++){
   var part=new SceneObjectPart{Tag=i};Parts[i]=part;var texture=new Primitive.TextureEntry(Probe.Id(1));texture.DefaultTexture.MaterialID=Probe.Id(2);texture.FaceTextures=new Primitive.TextureEntryFace[3];for(uint j=0;j<3;j++){var face=texture.CreateFace(j);face.TextureID=Probe.Id((int)j+3);face.MaterialID=Probe.Id((int)j+6);}
   part.Sh=new LiveShape{Tag=i,T=texture,R=new Primitive.RenderMaterials{entries=new[]{new Primitive.RenderMaterials.RenderMaterialEntry{id=Probe.Id(9)},new Primitive.RenderMaterials.RenderMaterialEntry{id=Probe.Id(-1)}},overrides=new[]{new Primitive.RenderMaterials.RenderMaterialOverrideEntry{data=Probe.Id(10)+"x"}}}};
   part.Particle=new byte[86];Probe.Id(13).ToBytes(part.Particle,36);part.Inv=new LiveInventory{Tag=i,Items=new(){new TaskInventoryItem{AssetID=Probe.Id(14),Type=0},new TaskInventoryItem{AssetID=Probe.Id(15),Type=6}}};part.Anim=new();foreach(int n in new[]{16,-2,0})part.Anim[Probe.Id(n)]=true;
   part.Attr=new LiveAttrs{Tag=i,Store=new OSDMap{{"Materials",new OSDArray{new OSDMap{{"Material",new OSDMap{{"NormMap",OSD.FromUUID(Probe.Id(17))},{"SpecMap",OSD.FromUUID(Probe.Id(18))}}}}}}}};
  }
  var first=Parts[0];switch(profile){case 2:group.List=null;break;case 3:group.List[0]=null;break;case 4:first.Sh=null;break;case 5:first.Sh.T=null;break;case 6:first.Sh.T.DefaultTexture=null;break;case 7:first.Sh.T.FaceTextures=null;break;case 8:first.Sh.T.FaceTextures=Array.Empty<Primitive.TextureEntryFace>();break;case 9:first.Sh.R=null;break;case 10:first.Sh.R.entries=null;break;case 11:first.Sh.R.overrides=null;break;case 12:first.Particle=null;break;case 13:case 32:first.Particle=new byte[90];first.Particle[0]=68;break;case 14:first.Inv=null;break;case 15:first.Inv.Items=null;break;case 16:first.Inv.Items[0]=null;break;case 17:first.Anim=null;break;case 18:first.Attr=null;break;case 20:first.Attr.Store=null;break;case 21:first.Attr.Store["Materials"]=new OSD();break;case 22:((OSDArray)first.Attr.Store["Materials"])[0]=null;break;case 23:((OSDArray)first.Attr.Store["Materials"])[0]=OSD.FromInteger(3);break;case 24:((OSDMap)((OSDArray)first.Attr.Store["Materials"])[0])["Material"]=null;break;case 25:((OSDMap)((OSDArray)first.Attr.Store["Materials"])[0])["Material"]=OSD.FromInteger(3);break;case 26:((OSDMap)((OSDMap)((OSDArray)first.Attr.Store["Materials"])[0])["Material"])["NormMap"]=null;break;case 28:{var array=(OSDArray)first.Attr.Store["Materials"];((OSDMap)((OSDMap)array[0])["Material"])["SpecMap"]=null;array.Add(new OSDMap{{"Material",new OSDMap{{"SpecMap",OSD.FromUUID(Probe.Id(19))}}}});break;}case 30:group=null;break;case 31:group.List[1]=null;break;case 33:group.List=Array.Empty<SceneObjectPart>();break;}
  return group;
 }
 public static void QueueMany(){Parts[0].Inv.Items.Clear();for(int n=100;n<200;n++)Parts[0].Inv.Items.Add(new TaskInventoryItem{AssetID=Probe.Id(n),Type=6});}
 public static string Call(UuidGatherer gatherer,int profile,int sides,int mutation,int nth){var group=Build(profile,sides,mutation,nth);Active=true;try{gatherer.AddForInspection(group);return "live:"+(Disposed?1:0);}finally{Active=false;}
 }
}

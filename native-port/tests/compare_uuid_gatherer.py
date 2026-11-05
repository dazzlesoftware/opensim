"""Original UuidGatherer queue/dispatch and real SDK embedded scanner comparisons."""
import argparse,base64,hashlib,itertools,json,random,re,struct,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract
ROOT=Path(__file__).resolve().parents[2]
def invoke(cmd,commands=None):
 r=subprocess.run(cmd,input=None if commands is None else '\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110)
 assert r.returncode==0,(r.returncode,r.stdout[-3000:],r.stderr)
 return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/Framework/Scenes/UuidGatherer.cs';text=source.read_text(encoding='utf-8-sig');sigs=['private static readonly HashSet<UUID> ToSkip','public bool Complete','public UUID? NextUuidToInspect','public UuidGatherer(IAssetService assetService, IDictionary<UUID, sbyte> collector) :', 'public UuidGatherer(IAssetService assetService, IDictionary<UUID, sbyte> collector, HashSet','public bool AddGathered(','public bool AddForInspection(UUID uuid)','public bool GatherNext(','public bool GatherAll(','private void GetAssetUuids(','private void AddForInspection(UUID assetUuid, sbyte assetType)','protected virtual AssetBase GetAsset(','public void AddForInspection(SceneObjectGroup sceneObject)', 'private void RecordMaterialsUuids(', 'private void RecordTextureEntryAssetUuids(', 'private unsafe void RecordEmbeddedAssetDataUuids(osUTF8 data)','private void RecordEmbeddedAssetDataUuids(ReadOnlySpan<char> data)']
 chunks=[extract(text,s) for s in sigs];chunks[0]+=';'
 generated=w/'ExtractedGatherer.cs';generated.write_text('using System;using System.Collections.Generic;using System.Text;using OpenMetaverse;using OpenMetaverse.StructuredData;partial class UuidGatherer {protected IAssetService m_assetService;protected Queue<UUID> m_assetUuidsToInspect;public IDictionary<UUID,sbyte> GatheredUuids{get;private set;}public HashSet<UUID> FailedUUIDs{get;private set;}public HashSet<UUID> UncertainAssetsUUIDs{get;private set;}public int possibleNotAssetCount{get;set;}public int ErrorCount{get;private set;}public int AssetGetCount;private bool verbose=true;'+''.join(chunks)+'}',encoding='utf-8')
 constants=text[text.index('private static readonly byte[] UUIDB'):text.index('/// <summary>',text.index('private static readonly byte[] UUIDB'))]
 formats=w/'ExtractedFormats.cs';formats.write_text("""using System;using System.Collections.Generic;using OpenMetaverse;partial class AssetFormatReference{static readonly byte[] wearableSeps=new byte[]{(byte)' ',(byte)'\\t'};"""+chunks[0]+constants+''.join(extract(text,s) for s in ['private void RecordWearableAssetUuids(', 'private void RecordGestureAssetUuids(', 'private void RecordMaterialAssetUuids(', 'private int getxmlNode(', 'private void RecordNoteCardEmbeddedAssetUuids(', 'private bool TryGetxmlUUIDValue(', 'private bool TryGetXMLBinary(', 'private void RecordSceneObjectAssetUuids(', 'private void RecordTextureEntryAssetUuids('])+'}',encoding='utf-8')
 shape_source=ROOT/'OpenSim/Framework/PrimitiveBaseShape.cs';shape_text=shape_source.read_text(encoding='utf-8-sig');shape=w/'ExtractedShape.cs';shape.write_text('using System;using OpenMetaverse;class PrimitiveBaseShape{bool _flexiEntry,_lightEntry,_sculptEntry,_projectionEntry,_meshFlagsEntry;int _flexiSoftness;byte _sculptType;uint _meshFlags;float _flexiTension,_flexiDrag,_flexiGravity,_flexiWind,_flexiForceX,_flexiForceY,_flexiForceZ,_lightIntensity,_lightColorA,_lightColorR,_lightColorG,_lightColorB,_lightRadius,_lightCutoff,_lightFalloff,_projectionFOV,_projectionFocus,_projectionAmb;UUID _sculptTexture,_projectionTextureID;public UUID ProjectionTextureUUID=>_projectionTextureID;public Primitive.RenderMaterials RenderMaterials;public Primitive.ReflectionProbe ReflectionProbe;'+''.join(extract(shape_text,s) for s in ['public void ReadInExtraParamsBytes(', 'public void ReadSculptData(', 'public void ReadFlexiData(', 'public void ReadLightData(', 'public void ReadProjectionData(', 'public void ReadMeshFlagsData(', 'public void ReadReflectionProbe(', 'public int ReadRenderMaterials('])+'}',encoding='utf-8')
 sl=ROOT/'OpenSim/Framework/SLUtil.cs';sltext=sl.read_text(encoding='utf-8-sig');util=ROOT/'OpenSim/Framework/Util.cs';utext=util.read_text(encoding='utf-8-sig');note=w/'ExtractedNote.cs';start=sltext.index('public static List<UUID> GetEmbeddedAssetIDs(');end=sltext.index('/// <summary>',start);fields=sltext[sltext.index('static readonly char[] seps'):sltext.index('[MethodImpl',sltext.index('static readonly char[] seps'))];note.write_text('using System;using System.Text;using System.Collections.Generic;using System.Runtime.CompilerServices;using OpenMetaverse;static class SLUtil{'+fields+extract(sltext,'static int getField(')+extract(sltext,'private static UUID deMoronize(')+sltext[start:end]+'}static class Util{public static Encoding UTF8=Encoding.UTF8;'+extract(utext,'public static List<UUID> GetUUIDsOnString(ReadOnlySpan<char> s)')+'}',encoding='utf-8')
 flotsam=ROOT/'OpenSim/Region/CoreModules/Asset/FlotsamAssetCache.cs';appearance_source=ROOT/'OpenSim/Framework/AvatarAppearance.cs';bake_field=next(line.strip() for line in appearance_source.read_text(encoding='utf-8-sig').splitlines() if 'public readonly static byte[] BAKE_INDICES =' in line);scan=w/'ExtractedSceneScan.cs';scan.write_text('using System;using System.Collections.Generic;using OpenMetaverse;partial class AvatarAppearance{'+bake_field+'}partial class FlotsamAssetCache{'+extract(flotsam.read_text(encoding='utf-8-sig'),'private Dictionary<UUID, sbyte> GatherSceneAssets(')+'}',encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',AllowUnsafeBlocks='true').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,formats,note,shape,scan,ROOT/'native-port/tests/scene_asset_gather_reference.cs',ROOT/'native-port/tests/uuid_gatherer_reference.cs',ROOT/'native-port/tests/live_asset_gather_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','OpenMetaverse','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'GathererReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);invoke([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet']);managed=[a.dotnet,str(w/'app/GathererReference.dll')]
 commands=[]
 def add(*rows):commands.extend(rows)
 skip_ids=re.findall(r'new UUID\("([^"]+)"\)',chunks[0])+['968cbad0-4dad-d64e-71b5-72bf13ad051a'];add('init',*[f'skip {s}' for s in skip_ids])
 for type,mode,length,uncertain,report in itertools.product([-128,-2,-1,0,1,5,6,7,10,13,20,21,49,56,57,58,127],range(4),[0,1,35,36,37,64],range(2),range(2)):
  add('init',f'asset 1 {mode} {type} {length}',*(['uncertain 1'] if uncertain else []),'add 1',f'all {report}','add 1','typed 1 0','known 1 0','peek')
 for type,n,failed,uncertain in itertools.product(range(-128,128),[-3,-2,-1,0,1],range(2),range(2)):
  add('init',*([f'failed {n}'] if failed else []),*([f'uncertain {n}'] if uncertain else []),f'typed {n} {type}',f'known {n} {type}',f'add {n}')
 for point,nth,report,edge_mode in itertools.product(range(5),[1,2,3],range(2),range(5)):
  add('init','asset 1 3 6 36','asset 2 3 10 36','asset 3 3 21 36',f'edge 1 2 10 {edge_mode}','edge 2 1 6 0','edge 2 3 21 3','edge 3 4 0 1',f'fault {point} {nth}','add 1',f'all {report}','next','all 1','fault 0 1','add 5','all 0','select 1','add 1','typed 2 0','add 5','all 1')
 for errors,gets,possible in itertools.product([-2147483648,-1,0,2147483647],repeat=3):
  add('init',f'count {errors} {gets} {possible}','uncertain 1','failed 1','add 1','known 1 0','typed 1 0','unfail 1','asset 1 3 6 0','add 1','all 0','asset 2 1 6 36','add 2','all 1')
 rng=random.Random(844199)
 for _ in range(100):
  add('init')
  for n in range(1,13):add(f'asset {n} {rng.randrange(4)} {rng.choice([-2,0,5,6,7,10,13,21,56,57])} {rng.choice([0,1,35,36,64])}')
  for _ in range(120):
   op=rng.choice(['add','typed','known','uncertain','failed','unfail','uncollect','next','all','peek','select','clear-failed','edge','fault']);n=rng.randrange(1,13)
   if op in ['add','uncertain','failed','unfail','uncollect']:add(f'{op} {n}')
   elif op in ['typed','known']:add(f'{op} {n} {rng.choice([-128,-2,-1,0,6,10,57,127])}')
   elif op in ['all','select']:add(f'{op} {rng.randrange(2)}')
   elif op=='edge':add(f'edge {n} {rng.randrange(1,13)} {rng.choice([0,6,10,57])} {rng.randrange(5)}')
   elif op=='fault':add(f'fault {rng.randrange(5)} {rng.randrange(1,4)}')
   else:add(op)
 def uid(n):return '00000000-0000-0000-0000-'+f'{n:012x}'
 payloads=['','x',uid(1),uid(1)+'x',uid(1)+'xx','x'+uid(1)+'x',uid(1)+uid(2)+'x',uid(1)+'x'+uid(2)+'x',uid(1)+'xx'+uid(2)+'x','0'*8+'-'+uid(1)+'x',uid(0)+'x',uid(1).upper()+'x','😀'+uid(1)+'x','漢'+uid(2)+'x','x'*64]
 for _ in range(1200):
  value=''.join(rng.choice(['x','-',uid(rng.randrange(13)),uid(rng.randrange(13)).upper(),'😀','漢',' ',':']) for _ in range(rng.randrange(1,9)));payloads.append(value)
 for value,kind in itertools.product(payloads,['scan','chars']):
  encoded=value.encode('utf-8' if kind=='scan' else 'utf-16le').hex() or '-';add('init',f'{kind} {encoded}','all 1')
 format_inputs=[]
 def format_case(kind,value):format_inputs.append((kind,value));add('init',f'format {kind} '+(value.hex() or '.'),'all 1')
 # Existing source byte-slice semantics: trailing line required, escaped CR/LF,
 # integer prefix/unchecked overflow and two different UUID parsing policies.
 for newline,count,tail in itertools.product(['\n','\r','\r\n'],['0','1','2','-1','+1','1x','x','4294967297'],['','\n','x\n']):
  text_value=newline.join(['2','0','0','trigger','replace',count,'0','name',uid(1),'0','1','name',uid(2),'0'])+tail;format_case(1,text_value.encode())
 for newline,count,spacing,ending in itertools.product(['\n','\r\n'],['0','1','2','-1','+1','1x','x','4294967297'],[' ','\t','  \t'],['','\n']):
  value=newline.join(['LLWearable version 22','name','description','{','parameters'+spacing+'1','0 0.5','textures'+spacing+count,'0'+spacing+uid(1),'1'+spacing+uid(2)])+ending;format_case(0,value.encode())
 for prefix,key,suffix in itertools.product(['','<llsd>','x','\\<','<!--x-->'],['uuid','UUID','uuidX','uuid/',' uuid ','/uuid'],['','<','</uuid>','</uuid>tail']):
  format_case(2,(prefix+'<'+key+'>'+uid(1)+suffix).encode())
 for kind in range(3):
  add('init',f'format {kind} -')
  for _ in range(900):
   fields=[rng.choice([uid(rng.randrange(13)),str(rng.choice([-2147483648,-1,0,1,2,2147483647,4294967297])),'0','textures','parameters','name','uuid','UUID','<uuid>','</uuid>','\\','x',' ', '\t', '\r','\n']) for _ in range(rng.randrange(1,25))]
   value=rng.choice(['',' ','\t','\n','\r\n']).join(fields).encode();format_case(kind,value)
 def note_value(count,entries,body):return ('Linden text version 2\n{\nLLEmbeddedItems version 1\n{\ncount '+count+'\n'+entries+'}\nText length 99\n'+body+'\n}').encode()
 for count,body in itertools.product(['0','-1','+0','x','1','2147483648','0x',' 0'],['',uid(1),uid(1)+'x',uid(1)+'x'+uid(2)+'x','😀'+uid(3)+'x']):format_case(3,note_value(count,'',body))
 key=bytes([60,17,94,81,4,244,82,60,159,166,152,175,241,3,71,48])
 import uuid
 for shadow,count,ending in itertools.product(range(2),['1','2','+1','1x','0','-1'],['','x',uid(3)+'x']):
  entries=''
  for n in [1,2]:
   encoded=uid(n)
   if shadow:encoded=str(uuid.UUID(bytes=bytes(a^b for a,b in zip(uuid.UUID(encoded).bytes,key))))
   entries+='ext char index 0\ninv_item 0\n{\npermissions 0\n{\n}\n'+('shadow_id' if shadow else 'asset_id')+'\t'+encoded+'\nsale_info 0\n{\n}\nname\tname|\ndesc\tdescription|\n}\n'
  value=note_value(count,entries,ending);format_case(3,value)
  for cut in range(0,len(value),7):format_case(3,value[:cut])
 for _ in range(800):
  value=bytearray(note_value(rng.choice(['0','1','2','-1','x']), '',uid(rng.randrange(13))+'x'))
  for _ in range(rng.randrange(1,7)):
   at=rng.randrange(len(value));value[at]=rng.choice([0,10,13,32,48,127,128,192,255])
  format_case(3,bytes(value))
 object_inputs=[]
 def object_case(value):object_inputs.append(value);format_case(4,value)
 def tag(name,value):return ('<'+name+'>'+value+'</'+name+'>').encode()
 def encoded_tag(name,data):return tag(name,base64.b64encode(data).decode())
 def uidbytes(n):return bytes.fromhex(uid(n).replace('-',''))
 # XML UUID readers deliberately accept matching prefixes, spaces and compact IDs.
 for name,wrapper,value,tail in itertools.product(['CollisionSound','CollisionSoundId','CollisionSoundX','SoundID','SoundIDX','SculptTexture','SculptTextureX'],['UUID','uuid','uuidX','UUID/','UUID '],[uid(0),uid(1),uid(2).replace('-',''),' '+uid(3)+'junk'],['','<SoundID><UUID>'+uid(4)+'</UUID></SoundID>']):object_case(tag(name,'<'+wrapper+'>'+value+'</'+wrapper+'>')+tail.encode())
 for n in range(8):
  line=invoke(managed,[f'texture {n}'])[0];payload=bytes.fromhex(line.split('|')[0]);object_case(encoded_tag('TextureEntry',payload))
  object_case(encoded_tag('TextureEntry',payload[:16]))
 for length,header in itertools.product(range(0,113),[0,68,69]):
  value=bytearray(length)
  if length>=4:value[:4]=struct.pack('<I',header)
  at=36 if length==86 else 40
  if length>=at+16:value[at:at+16]=uidbytes(1)
  object_case(encoded_tag('ParticleSystem',value))
 projection=lambda n:struct.pack('<HI',0x40,28)+uidbytes(n)+bytes(12)
 materials=lambda n,size=18:struct.pack('<Hi',0x80,size)+bytes([1,0])+uidbytes(n)
 extras=[bytes([0])*17,bytes([1])+projection(1),bytes([2])+projection(1)+projection(2),bytes([2])+projection(1)+projection(2)[:20],bytes([1])+materials(1),bytes([2])+materials(1)+bytes(4)+projection(2),bytes([2])+materials(1)+projection(2),bytes([1])+materials(1,2147483647),bytes([2])+materials(1,2147483647),bytes([2,255])+projection(1)]
 for opcode in [0x10,0x20,0x30,0x60,0x70,0x90]:
  length={0x10:16,0x20:16,0x30:17,0x60:17,0x70:4,0x90:9}[opcode];extras.append(bytes([2])+struct.pack('<HI',opcode,length)+bytes(length)+projection(1))
 for value in extras:
  object_case(encoded_tag('ExtraParams',value))
  # Do not invoke original SDK UUID reads beyond the array: those are unsafe.
  for cut in range(17,min(len(value),35)):
   if value[1]!=0x80:object_case(encoded_tag('ExtraParams',value[:cut]))
 for count in [0,1,2,3]:
  value=bytearray(180);value[:2]=struct.pack('<H',count)
  for at,n in [(2,1),(51,2),(100,3)]:value[at:at+16]=uidbytes(n)
  object_case(encoded_tag('SOPAnims',value)+b' '*256+b'<end/>')
 for ending in ['</TaskInventory>','</TaskInventoryX>','', '<SoundID><UUID>'+uid(3)+'</UUID></SoundID></TaskInventory>']:
  object_case(('<TaskInventory><Item><AssetID><UUID>'+uid(1)+'</UUID></AssetID></Item><AssetIDX><uuid>'+uid(2)+'</uuid></AssetIDX>'+ending).encode())
 for value in ['','x',uid(1),uid(1)+'x',uid(1)+'xx'+uid(2)+'x','😀'+uid(3)+'x']:object_case(encoded_tag('MatOvrd',value.encode()))
 for name,body in itertools.product(['ExtraParams','ParticleSystem','TextureEntry','SOPAnims','MatOvrd'],['',' ','\t\r\n','A','AA','AAA','AAAA','====','AB==','AAB=','AB=C','A===','AAAA====','x'*32]):
  if name=='SOPAnims' and body=='x'*32:continue # Separately retained SDK out-of-array read, not a deterministic oracle.
  object_case(tag(name,body)+tag('SoundID','<UUID>'+uid(6)+'</UUID>'))
 for value in object_inputs[:120]:
  for failed,uncertain in itertools.product([0,1],[0,1]):
   add('init',*(['failed 1','failed 2'] if failed else []),*(['uncertain 1','uncertain 2'] if uncertain else []),'format 4 '+value.hex(),'all 1')
 rng_object=random.Random(114419)
 for _ in range(1800):
  chosen=rng_object.sample(object_inputs[:300],rng_object.randrange(1,5));object_case(b'<SceneObjectGroup>'+b''.join(chosen)+b'</SceneObjectGroup>')
 # Byte-count and decoded UTF16-count are deliberately distinct source gates.
 for prefix,multibyte,count in itertools.product(['Linden text version 2','Linden text version 2\n{\n'],['漢','é','😀'],range(1,81)):
  format_case(3,(prefix+multibyte*count).encode())
 cultural_base=note_value('1','ext char index 0\ninv_item 0\n{\npermissions 0\n{\n}\nasset_id\t'+uid(1)+'\nsale_info 0\n{\n}\nname\tname|\ndesc\tdescription|\n}\n','x').decode()
 for field,marker,position in itertools.product(['asset_id','shadow_id','name','desc','Text length','ext char index'],['\x00','\u00ad','\u034f','\u200b','\u200c','\u200d','\ufeff','\u0600','\u06dd','\u070f','\u0d4e','\U000110bd'],range(3)):
  format_case(3,cultural_base.replace(field,field[:position]+marker+field[position:]).encode())
 index_inputs=[];texts=['','abc','a\x00bc','a\u00adbc','漢😀','Text length','é','e\u0301','æ','AE','\x00','\u200d','\ud800','\udc00','  name','na\u034fme']
 for source_value,pattern in itertools.product(texts,texts):index_inputs.append((source_value,pattern))
 for _ in range(3500):index_inputs.append((''.join(rng.choice(texts) for _ in range(rng.randrange(1,7))),rng.choice(texts)))
 for marker in ['\u0600','\u06dd','\u070f','\u0d4e','\U000110bd','\u0301','\u200d','\U0001f1e6','\U0001f3fb']:
  for source_value,pattern in [('x'+marker+'y',marker),('x'+marker+'y','y'),(marker+'asset_id','asset_id'),('a'+marker,marker)]:index_inputs.append((source_value,pattern))
 index_inputs.extend([('x\r\ny','\r'),('x\r\ny','\n'),('x\r\ny','\r\n')])
 add('init')
 for source_value,pattern in index_inputs:add('index '+(source_value.encode('utf-16le',errors='surrogatepass').hex() or '-')+' '+(pattern.encode('utf-16le',errors='surrogatepass').hex() or '-'))
 connected_format_inputs=[]
 for kind,value in format_inputs:
  asset_type=[5,21,-2,7,6][kind];connected_format_inputs.append((asset_type,value))
 for value in payloads:connected_format_inputs.append((rng.choice([10,56,57]),value.encode()))
 for asset_type,value in connected_format_inputs:
  add('init','formats 1',f'asset 20 3 {asset_type} {len(value)}','raw 20 '+(value.hex() or '.'),'add 20','all 0','next','all 1')
 live_cases=0
 for profile,sides,failed,uncertain in itertools.product(range(34),[-2147483648,-1,0,1,2,3,45,100],range(2),range(2)):
  add('init',*(['failed 1','failed 2','failed 9'] if failed else []),*(['uncertain 1','uncertain 2','uncertain 9'] if uncertain else []),f'live {profile} {sides} 0 1','all 1');live_cases+=1
 for profile,point,nth in itertools.product([0,13,23,28,29,32],range(1,26),range(1,4)):
  add('init',f'fault {100+point} {nth}',f'live {profile} 3 0 1','fault 0 1','all 1');live_cases+=1
 for mutation,nth in itertools.product(range(1,4),range(1,25)):
  add('init',f'live 0 3 {mutation} {nth}','all 1');live_cases+=1
 scene_cases=0
 for profile,cancel,nth in itertools.product(range(26),[0,30,32,34,35,37,40,41,43,45,46,47,48,49,50,51],[1,2]):
  add('init',f'scene {profile} {cancel} {nth}');scene_cases+=1
 for profile,point,nth in itertools.product([0,13,15,20,25],list(range(30,52)),[1,2]):
  add('init',f'fault {200+point} {nth}',f'scene {profile} 0 1');scene_cases+=1
 for _ in range(200):add('init','known 1 0','known 2 0','uncollect 1','known 3 0','order')
 (w/'commands.txt').write_text('\n'.join(commands)+'\n',encoding='utf-8')
 outputs=[invoke(managed,commands),invoke([a.native,'--probe'],commands)];assert len(outputs[0])==len(outputs[1])==len(commands),(len(commands),len(outputs[0]),len(outputs[1]));diffs=[dict(index=i,command=c,source=s,native=n) for i,(c,s,n) in enumerate(zip(commands,*outputs)) if s!=n]
 # The original SDK UUID constructor reads 16 bytes without checking bounds.
 # For this malformed animation stream the second UUID lies outside its array.
 # Keep the observed difference and assert native bounded partial publication.
 unsafe_value=tag('SOPAnims','x'*32)+tag('SoundID','<UUID>'+uid(6)+'</UUID>');unsafe_commands=['init','format 4 '+unsafe_value.hex(),'all 1'];safe_rows=invoke([a.native,'--probe'],unsafe_commands)
 import uuid
 first=str(uuid.UUID(bytes=base64.b64decode('x'*32)[2:18]));assert safe_rows[1]=='format||6:1,'+first+':20|||0:0:0:1|' and safe_rows[2]=='0||6:1,'+first+':20|||0:0:0:1|'
 unsafe=dict(command=unsafe_commands,native=safe_rows,scope='Original SDK out-of-array UUID read is not reproduced; original observed divergence is retained in build/uuid-object-unsafe-original-observation.json. Native retains the first valid animation and subsequent sound, then bounds the malformed read. Arbitrary CLR memory reads are unverified.')
 report=dict(passed=not diffs,cases=len(commands),skipIds=len(skip_ids),embeddedInputs=len(payloads)*2,formatInputs=len(format_inputs)+3,invariantIndexInputs=len(index_inputs),connectedFormatInputs=len(connected_format_inputs),serializedObjectInputs=len(object_inputs),liveObjectInputs=live_cases,sceneInputs=scene_cases,unsafeOriginalRead=unsafe,sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [source,sl,util,shape_source,flotsam,appearance_source,ROOT/'bin/OpenMetaverseTypes.dll',ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverse.StructuredData.dll']},scope='Unchanged original queue/add/typed-dispatch/failure/gather methods and both embedded UUID scanners using real SDK osUTF8/osUTF8Slice/UUID. Controlled asset service/DTO/typed inspectors/logging compare callbacks, partial state, caller-shared dictionaries and sets, queue ordering, suppression persistence, original 167 skip IDs, Int32 wrap and cycles. Wearable/gesture/material methods also execute unchanged with actual SDK byte-slice semantics. Notecard extraction also compiles unchanged SLUtil/getField/shadow-ID and Util body scanner. Serialized-object methods and eight original extra-parameter readers also execute unchanged with actual SDK texture/particle constructors. Complete live object/scene gathering, arbitrary collector/CLR/UTF16/concurrency/platform semantics and production cleanup hosting remain pending; required inspectors cannot be omitted.',differences=diffs);(w/'uuid-gatherer-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,json.dumps(diffs[:3],indent=2);print(f'PASS: {len(commands)} original gatherer observations and {len(payloads)*2} embedded inputs')
if __name__=='__main__':main()

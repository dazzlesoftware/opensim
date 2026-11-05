"""Unchanged SceneObjectPart geometry methods, real bundled SculptType SDK enum."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def extract(text,signature):
 start=text.index(signature); brace=text.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
def invoke(cmd,data=None):
 r=subprocess.run(cmd,input=data,capture_output=True,text=True,encoding='utf-8',timeout=240)
 assert r.returncode==0,(r.returncode,r.stdout[-2000:],r.stderr[-2000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 part=ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectPart.cs';shape=ROOT/'OpenSim/Framework/PrimitiveBaseShape.cs';pt=part.read_text(encoding='utf-8-sig');st=shape.read_text(encoding='utf-8-sig')
 enums='\n'.join([extract(pt,'public enum PrimType : int')]+[extract(st,'public enum '+name+' : byte') for name in ['ProfileShape','HollowShape','Extrusion']]);methods='\n'.join(extract(pt,s) for s in ['public int GetNumberOfSides()','public PrimType GetPrimType(bool ignoreSculpt = false)'])
 source=(ROOT/'native-port/tests/prim_geometry_reference.cs').read_text(encoding='utf-8').replace('/* ENUMS */',enums).replace('/* METHODS */',methods);(w/'Reference.cs').write_text(source,encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(w/'Reference.cs'));ref=ET.SubElement(group,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll');cs=w/'PrimGeometryReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);invoke([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 rows=[];counts={}
 def add(v):rows.append(' '.join(map(str,v)))
 def base(profile=1,path=16):return [0,0,profile,path]+[0]*12
 def mark(name,last):counts[name]=len(rows)-last;return len(rows)
 last=0
 for profile,path in itertools.product(range(256),repeat=2):add(base(profile,path))
 last=mark('allProfilePathBytes',last)
 shapes=[(1,16),(0,16),(3,16),(5,32),(0,32),(1,32),(3,32)]
 for profile,path in shapes:
  for cut in range(65536):
   for index in [4,5]:v=base(profile,path);v[index]=cut;add(v)
 last=mark('allUnsignedCutValuesBothEndsAllTypes',last)
 boundaries=[0,1,12499,12500,12501,16666,16667,16668,24999,25000,33332,33333,33334,37499,37500,49999,50000,62500,65535]
 for (profile,path),begin,end,hollow in itertools.product(shapes,boundaries,boundaries,[0,1,65535]):
  v=base(profile,path);v[4:7]=[begin,end,hollow];add(v)
 last=mark('cutBoundaryCrossProducts',last)
 for sculpt,profile,path in itertools.product(range(256),range(256),[16,32]):
  v=base(profile,path);v[0:2]=[1,sculpt];v[4:7]=[16667,33333,1];add(v)
 last=mark('allSculptProfileBytes',last)
 for (profile,path),mask in itertools.product(shapes,range(256)):
  v=base(profile,path)
  for bit,index in enumerate([7,8,9,10,12,13,14,15]):v[index]=1 if mask&(1<<bit) else 0
  add(v)
 last=mark('allPathConditionCombinations',last)
 for (profile,path),index,value in itertools.product(shapes,range(9,15),range(-128,128)):
  v=base(profile,path);v[index]=value;add(v)
 last=mark('allSignedPathFieldValues',last)
 for (profile,path),begin,end in itertools.product(shapes,range(-128,128),range(-128,128)):
  v=base(profile,path);v[10:12]=[begin,end];add(v)
 last=mark('allTwistEndpointPairs',last)
 for (profile,path),index,value in itertools.product(shapes,[6,7,8],range(65536)):
  v=base(profile,path);v[index]=value;add(v)
 last=mark('allUnsignedHollowPathValues',last)
 for (profile,path),value in itertools.product(shapes,range(256)):
  v=base(profile,path);v[15]=value;add(v)
 last=mark('allRevolutionBytes',last)
 rng=random.Random(764393)
 for _ in range(20000):
  v=[rng.randrange(2),rng.randrange(256),rng.randrange(256),rng.randrange(256)]+[rng.randrange(65536) for _ in range(5)]+[rng.randrange(-128,128) for _ in range(6)]+[rng.randrange(256)];add(v)
 mark('randomFullDomain',last)
 data='\n'.join(rows)+'\n';expected=invoke([a.dotnet,str(w/'app/PrimGeometryReference.dll')],data);actual=invoke([a.native,'--probe'],data);assert len(expected)==len(actual)==len(rows)
 diffs=[dict(input=v,source=s,native=n) for v,s,n in zip(rows,expected,actual) if s!=n]
 report=dict(passed=not diffs,cases=len(rows),groups=counts,partSha256=hashlib.sha256(part.read_bytes()).hexdigest(),shapeSha256=hashlib.sha256(shape.read_bytes()).hexdigest(),sdkSha256=hashlib.sha256((ROOT/'bin/OpenMetaverseTypes.dll').read_bytes()).hexdigest(),methodsSha256=hashlib.sha256(methods.encode()).hexdigest(),referenceSha256=hashlib.sha256(source.encode()).hexdigest(),scope='Unchanged GetPrimType/GetNumberOfSides and framework enum declarations with actual bundled SDK SculptType. Typed getter-value DTO deliberately bypasses PrimitiveBaseShape setters: input profile bytes are already-exposed getter values, not packet bytes. No shape setter/extra-parameter decoder, null Shape, concurrent mutation, mesher or visual topology claim.',differences=diffs[:10]);(w/'prim-geometry-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,diffs[:3];print(f'PASS: {len(rows)} original prim geometry observations')
if __name__=='__main__':main()

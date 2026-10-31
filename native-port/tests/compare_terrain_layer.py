"""Exact LayerData bytes from whole original OpenSim compressor + SDK BitPack."""
import argparse,hashlib,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 sources=['OpenSim/Framework/TerrainData.cs','OpenSim/Framework/TerrainTaintsArray.cs','OpenSim/Region/Framework/Scenes/TerrainCompressor.cs']
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',AllowUnsafeBlocks='true',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [*[ROOT/s for s in sources],ROOT/'native-port/tests/terrain_layer_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'TerrainLayerReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[];rng=random.Random(81239)
 def add(w,h,mode,seed,patches):commands.append(' '.join(map(str,[w,h,mode,seed,len(patches),*[v for pair in patches for v in pair]])))
 for width,height in [(256,256),(512,256),(256,512),(512,512),(32,48)]:
  for mode in range(4):
   add(width,height,mode,1,[])
   add(width,height,mode,1,[(0,0)])
   add(width,height,mode,17,[(width//16-1,height//16-1)])
   add(width,height,mode,3,[(x,y) for y in range(height//16) for x in range(width//16)])
 for i in range(100):
  width,height=rng.choice([(256,256),(512,256),(256,512)]);add(width,height,rng.randrange(4),rng.randrange(50),[(rng.randrange(width//16),rng.randrange(height//16)) for _ in range(rng.randrange(1,25))])
 rows=[]
 for cmd in [[a.dotnet,str(w/'app/TerrainLayerReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands),(len(rows[0]),len(rows[1]))
 diff=[dict(command=c,source=s[:120],native=n[:120]) for c,s,n in zip(commands,*rows) if s!=n]
 report=dict(passed=not diff,streams=len(commands),packets=sum(row.count(';') for row in rows[0]),bytes=sum(sum((len(packet.split(':')[1])//2) for packet in row.split(';') if packet) for row in rows[0]),sourceHashes={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources+['bin/OpenMetaverse.dll','bin/OpenMetaverseTypes.dll']},scope='Original whole terrain compressor, TerrainData and taints compiled against actual SDK BitPack; exact land/extended layer payload bytes and source LLClientView batching projection. Flat, near-flat, patterned, ramps, boundary IDs and full-region streams. Transport delivery, viewer decoding, nonfinite terrain, wind/cloud, actual Unix execution remain separate.',differences=diff[:3])
 (w/'terrain-layer-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diff,json.dumps(diff[:1],indent=2)
 print(f'PASS: {len(commands)} streams, {report["packets"]} packets, {report["bytes"]} exact source payload bytes')
if __name__=='__main__':main()

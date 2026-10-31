"""Actual bundled SDK Utils.BytesToString + CLR UTF8 fallback, no extracted stand-in."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/script_asset_text_reference.cs'))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'ScriptAssetTextReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 cases=['null','']+[bytes([a]).hex() for a in range(256)]+[bytes(pair).hex() for pair in itertools.product(range(256),repeat=2)]
 # Every three-byte lead and every second byte, with terminal boundary bytes;
 # UTF8 four-byte leads likewise and selected third/fourth boundaries.
 edges=[0,0x41,0x7f,0x80,0x8f,0x90,0x9f,0xa0,0xbf,0xc0,0xff]
 cases += [bytes(v).hex() for v in itertools.product(range(0xe0,0xf0),range(256),edges)]
 cases += [bytes(v).hex() for v in itertools.product(range(0xf0,0xf6),edges,edges,edges)]
 cases += [(s+suffix).hex() for s in [b'',b'abc',b'a\0b',bytes.fromhex('efbbbf'),bytes.fromhex('e282'),bytes.fromhex('f09f9883'),bytes.fromhex('eda080')] for suffix in [b'\0',b'\0\0',b'\0\0\0']]
 randomizer=random.Random(684327);cases += [randomizer.randbytes(randomizer.randrange(0,128)).hex() for _ in range(10000)]
 rows=[]
 for cmd in [[a.dotnet,str(w/'app/ScriptAssetTextReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(cases)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 diffs=[dict(input=c,source=s,native=n) for c,s,n in zip(cases,*rows) if s!=n]
 files=[ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll',ROOT/'native-port/src/text.cpp']
 report=dict(passed=not diffs,cases=len(cases),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Actual bundled SDK Utils.BytesToString(byte[]) followed by Encoding.UTF8.GetBytes vs native UTF8 result; null exception normalized. Exhaustive one/two-byte strings, all 3-byte leads and second bytes with boundaries, selected 4-byte ranges, truncated/overlong/surrogate/outofrange sequences, BOM, embedded/repeated trailing zeros and deterministic arbitrary byte strings. Framework text conversion reused unchanged.',differences=diffs[:3]);(w/'script-asset-text-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {len(cases)} actual SDK script asset text observations')
if __name__=='__main__':main()

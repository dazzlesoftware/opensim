"""Whole original module, actual Nini/Caps/CapsHandlers/SDK and controlled scene/file/HTTP edges."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/ClientStack/Linden/Caps/SimulatorFeaturesModule.cs';s=source.read_text(encoding='utf-8-sig').replace('private static readonly ILog m_log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);','private static readonly global::FeatureLog m_log = new();').replace('UUID.Random()','FeatureContext.Current.RandomUUID()').replace('File.Exists(','FeatureFiles.Exists(').replace('File.OpenText(','FeatureFiles.OpenText(');wrapper=w/'Module.cs';wrapper.write_text(s,encoding='utf-8')
 cap=w/'Caps.cs';cap.write_text((ROOT/'OpenSim/Capabilities/Caps.cs').read_text(encoding='utf-8-sig').replace('private static readonly ILog m_log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);','private static readonly global::CapLog m_log = new();').replace('private ManualResetEvent m_capsActive = new(false);','private global::ControlledActivation m_capsActive = new(false);'),encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false','StartupObject':'FeatureReference'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [wrapper,cap,ROOT/'OpenSim/Capabilities/CapsHandlers.cs',ROOT/'native-port/tests/caps_reference.cs',ROOT/'native-port/tests/simulator_features_reference.cs',ROOT/'OpenSim/Framework/Constants.cs',ROOT/'OpenSim/Region/Framework/Interfaces/ISimulatorFeaturesModule.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','OpenMetaverse','OpenMetaverse.StructuredData','Nini','log4net']:
  r=ET.SubElement(group,'Reference',Include=name);ET.SubElement(r,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 path=w/'Features.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 for i in range(4):(w/f'config-{i}.ini').write_text('[SimulatorFeatures]\nExportSupported='+str(bool(i&2)).lower()+'\nScriptSyntax='+str(not bool(i&1)).lower()+'\n',encoding='utf-8')
 ops=['IG','IFSTmH','IXUVH','IFTDUVG','IFJIH','IACCh','IABRCE','ILH','FILTH','IXDLH','I123H','I143H','I4H','IJICYYZ','NIRPO','IxHG','IzHG']
 cases=[f'{p}::{op}' for p,op in itertools.product(range(104),ops)]
 cases += [f'{p}:{failure}:IIYC' for p,failure in itertools.product([4,16,20,28,40,44,64,76],['exists','open','read0','read1','read2','dispose','log'])]
 cases += [f'{30+104*i}::IXH' for i in range(8)]
 rng=random.Random(701);cases += [f'{rng.randrange(104)}::I'+''.join(rng.choices('ABRLMFCFXDTUVGSH1234YChJm',k=15))+'HGY' for _ in range(200)]
 cyclic=[c for c in cases if c=='18::IAm1GCRFCh1XT2MCHGY'];cases=[c for c in cases if c not in cyclic]
 rows=[]
 for cmd in [[a.dotnet,str(w/'app/Features.dll'),str(w)],[a.native,str(w)]]:
  r=subprocess.run(cmd,input='\n'.join(cases)+'\n',capture_output=True,text=True,timeout=120);assert r.returncode==0,(len(r.stdout.splitlines()),cases[len(r.stdout.splitlines())] if len(r.stdout.splitlines())<len(cases) else 'end',r.stderr[:400]);rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases),(len(rows[0]),len(rows[1]),len(cases))
 file_cases=[];text='01234567-89ab-cdef-0123-456789abcdef\r\n <!-- comment --> \n<map>\r <string>世界😀</string> \r\n</map>\n'
 for encoding,bom in [('utf-8',b''),('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\0\0'),('utf-32-be',b'\0\0\xfe\xff')]:file_cases.append((encoding+'-'+bom.hex(),bom+text.encode(encoding)))
 file_cases += [('utf8-invalid',text.encode()+b'\xff\xed\xa0\x80'),('utf16-truncated',b'\xff\xfe'+text.encode('utf-16-le')+b'\xff'),('utf32-truncated',b'\0\0\xfe\xff'+text.encode('utf-32-be')+b'\xff'),('empty',b'')]
 for label,payload in file_cases:
  (w/'ScriptSyntax.xml').write_bytes(payload);case='1000::IIYC';cases.append('file:'+label)
  for i,cmd in enumerate([[a.dotnet,str(w/'app/Features.dll'),str(w)],[str(Path(a.native).resolve()),str(w)]]):
   r=subprocess.run(cmd,input=case+'\n',capture_output=True,text=True,timeout=20,cwd=w);assert r.returncode==0,(label,r.stderr[:200]);rows[i].append(r.stdout.strip())
 cyclic_results=[]
 for case in cyclic:
  states=[]
  for cmd in [[a.dotnet,str(w/'app/Features.dll'),str(w)],[str(Path(a.native).resolve()),str(w)]]:
   r=subprocess.run(cmd,input=case+'\n',capture_output=True,text=True,timeout=20);assert 'ERROR: AddressSanitizer' not in r.stderr;states.append({'exit':r.returncode,'stackOverflow':'Stack overflow' in r.stderr})
  assert states[0]['exit']!=0 and states[0]['stackOverflow'] and states[1]['exit']==0,states
  cyclic_results.append({'case':case,'source':states[0],'native':states[1],'parityVerified':False})
 diffs=[{'case':c,'source':x,'native':y} for c,x,y in zip(cases,*rows) if x!=y];(w/'simulator-features-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'sourceHash':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Whole original module with logger/RNG/filesystem boundary projections, actual Nini/Caps/CapsHandlers/SDK XML and Constants. Controlled scene/GridInfo/HTTP adapters. Full file encodings, callbacks/concurrency/SDK special types and world/platform parity pending.','deferredCyclicGraphCases':cyclic_results,'differences':diffs[:5]},indent=2),encoding='utf-8');assert not diffs,str([v['case'] for v in diffs[:15]])+' count='+str(len(diffs));print(f'PASS: {len(cases)} simulator feature observations')
if __name__=='__main__':main()

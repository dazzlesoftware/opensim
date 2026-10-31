"""Original SeedCapRequest method, actual Caps/CapsHandlers and SDK XML parser/writer."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import extract
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/ClientStack/Linden/Caps/BunchOfCaps/BunchOfCaps.cs';method=extract(source.read_text(encoding='utf-8-sig'),r'public void SeedCapRequest\(');wrapper=w/'Seed.cs';wrapper.write_text('using OpenSim.Framework;using System;using System.Net;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using Caps=OpenSim.Framework.Capabilities.Caps;public partial class SeedMethod{'+method+'}',encoding='utf-8')
 cap=w/'Caps.cs';cap.write_text((ROOT/'OpenSim/Capabilities/Caps.cs').read_text(encoding='utf-8-sig').replace('private static readonly ILog m_log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);','private static readonly global::CapLog m_log = new();').replace('private ManualResetEvent m_capsActive = new(false);','private global::ControlledActivation m_capsActive = new(false);'),encoding='utf-8');adapters=w/'Adapters.cs';adapters.write_text((ROOT/'native-port/tests/caps_reference.cs').read_text(encoding='utf-8').replace('class Context {','public class Context {'),encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false','StartupObject':'SeedReference'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [ROOT/'OpenSim/Framework/Constants.cs',wrapper,cap,adapters,ROOT/'OpenSim/Capabilities/CapsHandlers.cs',ROOT/'native-port/tests/seed_caps_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','OpenMetaverse','OpenMetaverse.StructuredData','log4net']:
  r=ET.SubElement(group,'Reference',Include=name);ET.SubElement(r,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'SeedReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');r=subprocess.run([a.dotnet,'build',str(cs),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 names=['A','ObjectAnimation','EnvironmentSettings','ExtEnvironment','ModifyMaterialParams','ViewerBenefits','VTPBR','VETPBR','SEED','0','1','NaN','∞','-∞','1970-01-01T00:00:00Z','00000000-0000-0000-0000-000000000000','aGVsbG8=','http://example.com/','試験😀','<raw>'];cases=[]
 def add(xml,**kwargs):cases.append(dict(caps=names,xml=(xml.encode() if isinstance(xml,str) else xml).hex(),**kwargs))
 def array(items):return '<llsd><array>'+items+'</array></llsd>'
 baseline=array(''.join('<string>'+n+'</string>' for n in names[:9]+['External','Poll','A','SEED']))
 for active,allow,method,type in itertools.product([True,False],[True,False],['POST','GET','post','PUT'],['application/llsd+xml','application/llsd+xml; charset=utf-8','APPLICATION/LLSD+XML','',None]):add(baseline,active=active,allow=allow,method=method,type=type)
 for x,y,flags in itertools.product([0,256,512],[0,256,512],[0,1,0xffffffff]):add(baseline,x=x,y=y,flags=flags)
 for name in names+['','Poll','External','unknown','seed','ViewerBenefits ','vtpbr']:
  for flags in [0,0xffffffff]:add(array('<string>'+name.replace('&','&amp;').replace('<','&lt;')+'</string>'),flags=flags)
 for tag,values in {'string':['','A',' A ','試験😀','&lt;raw&gt;','SEED','ViewerBenefits','\r\nA\r\n'],'boolean':['','true','1','TRUE','false','bad',' true '],'integer':['','1','+1','-1','bad','2147483648','1,000',' 1 '],'real':['','1.5','NaN','INF','Infinity','-Infinity','bad',' 1 '],'uuid':['','bad','00000000-0000-0000-0000-000000000000'],'date':['','bad','1970-01-01T00:00:00Z','2020-02-03'],'binary':['','aGVsbG8=','bad','@@@@'],'uri':['','http://EXAMPLE.com','relative/path','http://[bad','::'],'undef':['',' ','A'],'unknown':['','A']}.items():
  for value in values:add(array(f'<{tag}>{value}</{tag}>'))
  add(array(f'<{tag}/>'))
 for xml in ['', '<llsd/>','<llsd></llsd>','<array/>','<llsd><string>A</string></llsd>','<llsd><unknown></unknown></llsd>',array('<string>VTPBR</string><unknown></unknown><string>A</string>'),array('<array><string>A</string></array><map><key>a</key><string>A</string></map>'),array('<map><key/><string>A</string></map>'),array('<binary encoding="hex">aGVsbG8=</binary>'),array('<binary encoding="hex"/>'),array('<string>A<!--comment-->A</string>'),array('<string><![CDATA[A]]></string>'),array('<!--comment--><string>A</string>'),array('<?test value?><string>A</string>'),'<anything><array><string>A</string></array></anything>','<llsd><array/></llsd><invalid', '<llsd><array/><string>A</string></llsd>','<llsd><array/><invalid', '<llsd xmlns:s="urn:test"><s:array><s:string>A</s:string></s:array></llsd>', '<llsd><s:array><s:string>A</s:string></s:array></llsd>', '<!DOCTYPE llsd><llsd><array><string>A</string></array></llsd>', '<llsd><array><string>&unknown;</string></array></llsd>']:
  add(xml)
 for value,suffix in itertools.product(['<array/>','<array></array>','<array><string>A</string></array>'],['',' ','<!--ok-->','<string>','<string bad="value">','<string bad="&unknown;">','<s:string>','</wrong>','<?test value?>','<invalid','<string>unfinished','garbage','<!--unfinished']):add('<llsd>'+value+suffix)
 for xml in [array('<string a="1" a="2">A</string>'),array('<string>A&#0;</string>'),array('<string>&amp;unknown;</string>'),array('<string>&#65;</string>'),array('<string>&#x41;</string>'),array('<string a="&unknown;">A</string>'),array('<string a="&amp;">A</string>'),array('<string a="&gt; &amp;">A</string>'),array('<string a=" > &amp;">A</string>')]:add(xml)
 for value in ['∞','-∞','+∞','1,000','1e500','+NaN','-NaN','nan',' 1e-50 ']:add(array('<real>'+value+'</real>'))
 for name in ['SEED','VTPBR','VETPBR','ObjectAnimation','ViewerBenefits','EnvironmentSettings','ExtEnvironment','ModifyMaterialParams']:
  for tail in ['<unknown></unknown>','<unknown/>','<integer>bad</integer>','<string>'+name+'</string>']:add(array('<string>'+name+'</string>'+tail))
 features=['ViewerBenefits','VTPBR','VETPBR','ObjectAnimation','EnvironmentSettings','ExtEnvironment','ModifyMaterialParams','SEED']
 for mask,x,y in itertools.product(range(256),[256,512],[256,512]):add(array(''.join('<string>'+name+'</string>' for bit,name in enumerate(features) if mask&(1<<bit))),x=x,y=y)
 for text in ['2020-02-30','0000-01-01','2020-02-03T01:02:03','2020-02-03T01:02:03.1','2020-02-03T01:02:03.001','2020-02-03T01:02:03.1234567','2020-02-03T01:02:60','02/03/2020']:add(array('<date>'+text+'</date>'))
 for text in ['&#0;','&#1;','&#xFFFF;','&#xD800;','&#x110000;','A&#0;','&#x1F600;','A\r\nB','A\rB','A&amp;B','A<![CDATA[B]]>C']:add(array('<string>'+text+'</string>'))
 for failure in ['input','wait120000','x','y','check:00000000-0000-0000-0000-000000000001:127.0.0.1:1234']:add(baseline,failure=failure)
 for encoding in ['utf-8-sig','utf-16','utf-16-be']:add(array('<string>A</string>').encode(encoding))
 commands=[json.dumps(c,ensure_ascii=False,separators=(',',':')).encode().hex() for c in cases];rows=[]
 for cmd in [[a.dotnet,str(w/'app/SeedReference.dll')],[a.native]]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120);assert r.returncode==0,r.stdout[-2000:]+r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 diffs=[{'input':c,'source':x,'native':y} for c,x,y in zip(cases,*rows) if x!=y]
 (w/'seed-caps-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Original SeedCapRequest body with real original Caps/CapsHandlers and SDK XML reader/writer. Caps logger/activation field projections and controlled scene/request/response/log/listener. Full BunchOfCaps, production client authorization and general XML/CLR/platform integration remain pending.','differences':diffs},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:3],ensure_ascii=False))
 print(f'PASS: {len(cases)} seed capability source observations')
if __name__=='__main__':main()

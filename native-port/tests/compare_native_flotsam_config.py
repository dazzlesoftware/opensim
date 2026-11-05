"""Actual bundled Nini typed getters against the native resolved-INI adapter."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def call(command,rows=None):
 r=subprocess.run(command,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110);assert r.returncode==0,(r.returncode,r.stdout[-2000:],r.stderr[-2000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/native_flotsam_config_reference.cs'));ref=ET.SubElement(group,'Reference',Include='Nini');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/Nini.dll');cs=w/'NativeFlotsamConfigReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 values=['','true','false','True','FALSE','Yes','No','On','Off','1','0',' true ','yes\0','\0true','t\u00adrue','tr\u200bue','\ufefftrue','+0','-0','+1','-1','2147483647','2147483648','-2147483648','-2147483649','9223372036854775807','9223372036854775808','-9223372036854775808','-9223372036854775809','99999999999999999999999999x','1,000','1,,2','1.0','1e3','1e9999','1e-9999','NaN','+NaN','-NaN','Infinity','-Infinity','+Infinity','∞','\u00a01','1\u00a0',' 1\0','1\0\0','1\0 ','1\t\0','1\0x','１２','١٢','0x10','+-1','  -2  ','- 2','-','+','1_000']
 values += [prefix+value+suffix for prefix,value,suffix in itertools.product(['',' ','\t','\r\n','\u00a0'],['true','1','-1','1.25','NaN'],['',' ','\0','\0\0','\t\0'])]
 rng=random.Random(491783)
 for _ in range(3000):values.append(str(rng.randrange(-2**80,2**80)))
 for _ in range(3000):values.append(''.join(rng.choice('0123456789eE+-,. \txyz') for _ in range(rng.randrange(1,45))))
 rows=[f'{op} {aliases} 1 '+(value.encode().hex() or '.') for op,aliases,value in itertools.product(['s','n','b','i','l','r'],range(2),values)]
 for code in range(65536):
  if not 0xd800<=code<=0xdfff:rows.append('b 1 1 '+('tr'+chr(code)+'ue').encode().hex())
 for value in ['1'+'0'*100000+'e-200000','0.'+'0'*100000+'1e200000','0e'+'9'*1000,'-0e'+'9'*1000]:rows.append('r 1 1 '+value.encode().hex())
 rows += [f'{op} {aliases} 0 .' for op,aliases in itertools.product(['s','n','b','i','l','r'],range(2))]
 outputs=[call([a.dotnet,str(w/'app/NativeFlotsamConfigReference.dll')],rows),call([a.native,'--probe'],rows)];assert len(outputs[0])==len(outputs[1])==len(rows)
 differences=[dict(command=c,source=s,native=n) for c,s,n in zip(rows,*outputs) if s!=n]
 report=dict(passed=not differences,cases=len(rows),sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'bin/Nini.dll',ROOT/'OpenSim/Region/Application/Application.cs']},scope='Actual bundled Nini getters with OpenSim invariant startup culture and optional application boolean aliases. Native programmatic resolved INI values, missing/default/null strings, integer/long range and format priority, double bits, alias Unicode/ignorable behavior, error type/message/HRESULT. General INI parsing/includes/expansion and arbitrary non-invariant caller culture remain separate obligations.',differences=differences[:8]);(w/'native-flotsam-config-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not differences,json.dumps(differences[:3]);print('PASS:',len(rows),'actual Nini typed-value observations')
if __name__=='__main__':main()

"""Whole original local ConfigurationLoader composed with actual Nini/env/network."""
import argparse,hashlib,itertools,json,re,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):
 if s is None:return 'null'
 b=s.encode('utf-16-le','surrogatepass');return ''.join(f'{b[i]+256*b[i+1]:04x}' for i in range(0,len(b),2)) or '-'
def call(cmd,row=None,cwd=None):
 p=subprocess.run(cmd,input=None if row is None else row+'\n',capture_output=True,text=True,encoding='utf-8',cwd=cwd,timeout=60);assert p.returncode==0,(p.returncode,p.stdout[-2500:],p.stderr[-2500:]);return p.stdout.splitlines()
def extract(text,signature):
 start=text.index(signature);brace=text.index('{',start);depth=1;n=brace+1
 while depth:
  if text[n]=='{':depth+=1
  elif text[n]=='}':depth-=1
  n+=1
 return text[start:n]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native');p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--source-only',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 if a.native:a.native=str(Path(a.native).resolve())
 (w/'Aliases.cs').write_text('global using Path=Shim.Path;global using File=Shim.File;global using Directory=Shim.Directory;global using Environment=Shim.Environment;global using XmlReader=Shim.XmlReader;',encoding='utf-8')
 util=(ROOT/'OpenSim/Framework/Util.cs').read_text(encoding='utf-8');glob=extract(util,'public static string[] Glob(string path)').replace('Path.','System.IO.Path.').replace('Directory.','System.IO.Directory.')
 env=extract(util,'public static void MergeEnvironmentToConfig(IConfigSource ConfigSource)')
 (w/'Util.cs').write_text('using System;using System.Collections.Generic;using Nini.Config;static class SourceUtil{'+glob+'}namespace OpenSim.Framework{public static partial class Util{'+env+'}}',encoding='utf-8')
 app_source=(ROOT/'OpenSim/Region/Application/Application.cs').read_text(encoding='utf-8')
 declaration=re.search(r'public static string iniFilePath\s*=\s*"";',app_source).group(0)
 reference=(ROOT/'native-port/tests/native_configuration_loader_reference.cs').read_text(encoding='utf-8').replace('public static string iniFilePath="";',declaration)
 (w/'Reference.cs').write_text(reference,encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [w/'Reference.cs',ROOT/'OpenSim/Region/Application/ConfigurationLoader.cs',ROOT/'OpenSim/Framework/ConfigSettings.cs',ROOT/'OpenSim/Framework/NetworkServersInfo.cs',w/'Aliases.cs',w/'Util.cs']:ET.SubElement(group,'Compile',Include=str(f))
 r=ET.SubElement(group,'Reference',Include='Nini');ET.SubElement(r,'HintPath').text=str(ROOT/'bin/Nini.dll');cs=w/'NativeConfigurationLoaderReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 base=w/'fixture';cwd=w/'process-cwd';base.mkdir(exist_ok=True);cwd.mkdir(exist_ok=True)
 for d in ['include','include/sub','overrides','combined-only']:(base/d).mkdir(exist_ok=True)
 (cwd/'overrides').mkdir(exist_ok=True)
 (cwd/'overrides/from-cwd.ini').write_text('[Data]\nvalue=raw-cwd-override\n[Network]\nhttp_listener_port=9023\n',encoding='utf-8')
 A='OSCP_VALUE';B='OSCP_MISSING';P='OSCP_PORT'
 files={
 'OpenSimDefaults.ini':'[Startup]\nphysics=master\nmeshing=master-mesh\n[Data]\nvalue=master\nempty=\n[Network]\nhttp_listener_port=9005\n',
 'OpenSim.ini':'[Startup]\nIn\u00adclude-first=include/a.ini\nInclude-second=include/b.ini\n[Data]\nvalue=main\n[Network]\nhttp_listener_sslport=-1\nhttps_listener=true\n',
 'include/a.ini':'[Data]\nvalue=include-a\nInclude-back=OpenSim.ini\nempty=\n',
 'include/b.ini':'[Data]\nvalue=include-b\n[Network]\nhttp_listener_cn=unicode-\u00e9\ncert_path=certificate\ncert_pass=fixture-secret\nhttps_port=-2\n',
 'include/é.ini':'[Unicode]\nvalue=unicode-file\n',
 'include/sub/child.ini':'[Data]\nvalue=child\nInclude-sibling=sibling.ini\n',
 'include/sub/sibling.ini':'[Data]\nvalue=incorrect-relative-sibling\n',
 'nested.ini':'[Startup]\nInclude-child=include/sub/child.ini\n',
 'unicode.ini':'[Startup]\nIn\u200dclude-u=include/é.ini\n',
 'combining.ini':'[Startup]\nInclude-\u0300ignored=include/a.ini\n[Data]\nvalue=must-stay\n',
 'missing-include.ini':'[Startup]\nInclude-missing=absent-file.ini\n',
 'wildcard.ini':'[Startup]\nInclude-all=include/*.ini\nInclude-none=absent*.ini\n',
 'no-startup.ini':'[Data]\nvalue=no-startup\nempty=\n',
 'no-network.ini':'[Startup]\nphysics=no-network\n[Data]\nvalue=no-network\n',
 'with-env.ini':f'[Startup]\nInclude-a=include/a.ini\n[Environment]\n{A}=ignored\n{B}=ignored\n{P}=ignored\n[Data]\nexpanded=${{Environment|{A}}}\nempty=${{Environment|{B}}}\n[Network]\nhttp_listener_port=${{Environment|{P}}}\n',
 'early-unknown.ini':f'[Environment]\n{A}=ignored\n[Data]\nexpanded=${{Future|value}}\n',
 'argv-env.ini':f'[Data]\nexpanded=${{Environment|{A}}}\n',
 'repeat.ini':f'[Environment]\n{A}=ignored\n[Data]\nvalue=repeated\n',
 'bad-port.ini':'[Network]\nhttp_listener_port=4294967295\n',
 'bad-bool.ini':'[Network]\nhttp_listener_ssl=Yes\n',
 'malformed.ini':'[unterminated',
 'remote-include.ini':'[Startup]\nInclude-remote=https://example.test/config?query#fragment\n',
 'overrides/override.INI':'[Data]\nvalue=override\n[Network]\nhttp_listener_port=9019\n',
 'overrides/skip.txt':'[Data]\nvalue=ignored-txt\n',
 }
 for name,text in files.items():(base/name).write_text(text,encoding='utf-8')
 fallback=w/'fallback';fallback.mkdir(exist_ok=True);(fallback/'OpenSim.xml').write_text('[Startup]\nphysics=fallback-ini\n',encoding='utf-8')
 xmlfallback=w/'xml-fallback';xmlfallback.mkdir(exist_ok=True);(xmlfallback/'OpenSim.xml').write_text('<Nini><Section Name="Startup" /></Nini>',encoding='utf-8')
 rows=[]
 def envops(value='environment',port='8011',missing=None):return ','.join(enc(k)+':'+enc(v) for k,v in [(A,value),(B,missing),(P,port)])
 def add(doc,mode='L',second='',directory=base,env=None,fault='-',nth=1):rows.append(f'{mode} {enc(doc)} {enc(str(directory))} {enc(second)} {enc(env if env is not None else envops())} {fault} {nth}')
 def argv(file,master='none',directory='missing',extra=''):return f'[Startup]\ninimaster={master}\ninifile={file}\ninidirectory={directory}\n'+extra
 for file,master,directory in itertools.product(['OpenSim.ini','with-env.ini','nested.ini','unicode.ini','combining.ini','missing-include.ini','wildcard.ini','no-startup.ini','no-network.ini','bad-port.ini','bad-bool.ini','malformed.ini','remote-include.ini'],['none','OpenSimDefaults.ini'],['missing','overrides',str(base/'overrides'),'combined-only']):add(argv(file,master,directory))
 for value,port in itertools.product(['environment','','${NoSuch|key}','raw-\ud800','\ufffe'],['8011','-1','2147483648']):add(argv('with-env.ini',extra='[Network]\nhttp_listener_ssl=On\n'),env=envops(value,port))
 add(argv('no-network.ini',extra='[Network]\nhttp_listener_ssl=Yes\nhttps_listener=On\nhttps_port=-1\n'))
 add(argv('bad-bool.ini',extra='[Network]\nhttp_listener_ssl=Yes\n'))
 add(argv('argv-env.ini',extra=f'[Environment]\n{A}=argv-literal\n'))
 add(argv('early-unknown.ini',extra='[Future]\nvalue=argv-too-late\n'))
 for master in ['none','missing-master.ini','https://example.test/master?x#fragment']:add(argv('OpenSim.ini',master))
 for directory in [fallback,xmlfallback]:add(argv('absent.ini'),directory=directory)
 add('[NoStartup]\nx=1\n')
 for remote in ['https://user:pass@example.test/config?query#fragment','http://example.test/raw-\ud800','http://example.test\\path']:add(argv(remote))
 add('[Startup]\n',mode='K',second='[Startup]\n')
 add(argv('no-network.ini'),mode='T',second=argv('no-network.ini'))
 add(argv('repeat.ini'),mode='T',second=argv('repeat.ini'))
 add(argv('no-network.ini'),mode='T',second=argv('with-env.ini'))
 add(argv('with-env.ini'),mode='T',second=argv('missing-master.ini',master='missing-master.ini'))
 good=argv('with-env.ini','OpenSimDefaults.ini',str(base/'overrides'),extra='physics=argv-physics\n[Network]\nhttp_listener_port=9047\n')
 for fault,nth in itertools.product(['configdir','combine','full','fileexists','direxists','files','extension','glob','log'],[1,2,3,5,9]):add(good,fault=fault,nth=nth)
 source=[];native=[]
 # Every case gets a fresh process: actual EnvMap.Instance remains persistent
 # within repeated-load cases and is never reset through reflection.
 for row in rows:
  result=call([a.dotnet,str(w/'app/NativeConfigurationLoaderReference.dll')],row,cwd);assert len(result)==1,result;source.extend(result)
  if not a.source_only:
   result=call([a.native,'--probe'],row,cwd);assert len(result)==1,result;native.extend(result)
 if a.source_only:(w/'source-results.json').write_text(json.dumps(dict(cases=len(rows),results=source),indent=2)+'\n',encoding='utf-8');print('SOURCE:',len(rows),'fresh-process original pipeline cases');return
 diff=[dict(index=i,command=c,source=s,native=n) for i,(c,s,n) in enumerate(zip(rows,source,native)) if s!=n];report=dict(passed=not diff,cases=len(rows),freshChildProcesses=True,applicationFieldDeclaration=declaration,scope='Unchanged full ConfigurationLoader + NetworkServersInfo + ConfigSettings + Util.Glob/MergeEnvironmentToConfig, actual Nini.dll; complete native local services and real process environment. Caller argv is an actual IniConfigSource interface input with explicit aliases, not a claim to port command-line tokenization. Remote XML provider is explicitly unsupported and exits through host policy. Global EnvMap persists inside repeat cases.',differences=diff[:8]);(w/'native-configuration-loader-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diff,f"{len(diff)} differences; see {w / 'native-configuration-loader-results.json'}";print('PASS:',len(rows),'fresh-process full native local configuration cases')
if __name__=='__main__':main()

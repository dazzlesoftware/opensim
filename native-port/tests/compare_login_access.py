"""Compare the original LLLoginService access blocks with the real native login entry point."""
from pathlib import Path
import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET

p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
login_path=root/'OpenSim/Services/LLLoginService/LLLoginService.cs';util_path=root/'OpenSim/Framework/Util.cs'
login=login_path.read_text(encoding='utf-8-sig');util=util_path.read_text(encoding='utf-8-sig')
constructor=login[login.index('string[] accessControlConfigSections ='):login.index('m_MessageUrl =')]
access=login[login.index('string clientNameToCheck;'):login.index('UserAccount account = m_UserAccountService.GetUserAccount(scopeID, firstName, lastName);')]
start=util.index('public static T GetConfigVarFromSections<T>(IConfigSource config, string varname, string[] sections, object val)');brace=util.index('{',start);depth=1;end=brace+1
while depth:
 if util[end]=='{':depth+=1
 if util[end]=='}':depth-=1
 end+=1
helper=util[start:end]
source='''using System;using System.Text.RegularExpressions;using Nini.Config;
class Log {public void Error(string s){} public void ErrorFormat(string s,params object[] args){} public void InfoFormat(string s,params object[] args){}}
class Util {static Log m_log=new Log();HELPER}
class LLFailedLoginResponse {public static string LoginBlockedProblem="blocked";}
class Reference {
static Log m_log=new Log();Regex m_AllowedClientsRegex,m_DeniedClientsRegex;string m_DeniedMacs,m_DeniedID0s;
Reference(IConfigSource config){CONSTRUCTOR}
string Check(string mac,string id0,string clientVersion,string channel){string firstName="Absent",lastName="Resident",curMac=mac.ToString();ACCESS return "user";}
static string Decode(string s){var chars=new char[s.Length/4];for(int i=0;i<chars.Length;i++)chars[i]=(char)Convert.ToUInt16(s.Substring(i*4,4),16);return new string(chars);}
static void Main(){OpenSim.Framework.Culture.SetCurrentCulture();OpenSim.Framework.Culture.SetDefaultCurrentCulture();string row;while((row=Console.ReadLine())!=null){try{var fields=row.Split('\\t');var config=new IniConfigSource();string[] keys={"DeniedMacs","DeniedID0s","AllowedClients","DeniedClients"};for(int i=0;i<8;i++)if(fields[i]!="-"){string name=i%2==0?"AccessControl":"LoginService";var section=config.Configs[name]??config.AddConfig(name);section.Set(keys[i/2],Decode(fields[i]));}Console.WriteLine(new Reference(config).Check(Decode(fields[8]),Decode(fields[9]),Decode(fields[10]),Decode(fields[11])));}catch(Exception e){Console.WriteLine("EXCEPTION:"+e.Message);}}}
}'''.replace('HELPER',helper).replace('CONSTRUCTOR',constructor).replace('ACCESS',access)
(work/'Reference.cs').write_text(source,encoding='utf-8')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,key).text=value
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(work/'Reference.cs'));ET.SubElement(items,'Compile',Include=str(root/'OpenSim/Framework/Culture.cs'));reference=ET.SubElement(items,'Reference',Include='Nini');ET.SubElement(reference,'HintPath').text=str(root/'bin/Nini.dll');ET.ElementTree(project).write(work/'Reference.csproj')
build=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);(work/'build.txt').write_text(build.stdout+build.stderr);assert build.returncode==0,build.stdout+build.stderr
def row(settings=None,mac='',id0='',version='1.0',channel='Viewer'):
 values=[None]*8
 for key,value in (settings or {}).items():values[key]=value
 values.extend([mac,id0,version,channel])
 return '\t'.join('-' if value is None else value.encode('utf-16-be').hex() for value in values)
cases=[]
# Every representable BMP scalar checks the source IsNullOrWhiteSpace guard.
for code in range(65536):
 if 0xd800<=code<=0xdfff:continue
 for index in [0,2]:cases.append(row({index:chr(code)}))
# Empty/substring/ordinal behavior and last-section-wins, including empty override.
values=[None,'',' ','\t\r\n','\u0085\u00a0\u2003\u3000','deny','DENY','abc,def','é中','😀','x\u200by']
for index in [0,2]:
 for shared,local,identifier in itertools.product(values,values,['','deny','DENY','abc','é','😀',' ']):
  cases.append(row({index:shared,index+1:local},**({'mac':identifier} if index==0 else {'id0':identifier})))
# Exercise precedence for both regex settings using syntax shared by both engines.
for index in [4,6]:
 for shared,local in itertools.product([None,'','Viewer','Other','['],repeat=2):
  for version,channel in [('1.0','Viewer'),('Other 2','Viewer'),('1.0','Other')]:cases.append(row({index:shared,index+1:local},version=version,channel=channel))
# .NET-specific groups, balancing, lookarounds, Unicode and inline options.
patterns=[r'(?<viewer>Viewer) \k<viewer>',r'(?<=Viewer )1',r'(?i:viewer)',
 r'(?<open>a)+(?<-open>b)+(?(open)(?!))',r'\p{Lu}+',r'\AViewer 1\z',
 r'(?n)(?<v>Viewer) (?(v)1|2)',r'(?<x>Viewer) (?:1|2)',r'(?i)k',
 r'\w+',r'^Viewer 1$',r'(?<x>[)',r'(?i:İ)',r'(?i:ı)',r'(?i:ſ)',r'(?i:K)']
for index in [4,6]:
 for expression,client in itertools.product(patterns,['Viewer 1','Viewer Viewer','aabb','aaab','é中 1','Other 2','Viewer 1\n','İ','ı','ſ','K','']):
  cases.append(row({index:expression},version=client,channel=''))
payload='\n'.join(cases)+'\n';(work/'input.txt').write_text(payload,encoding='utf-8')
outputs={}
for name,command in [('reference',[a.dotnet,str(work/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 result=subprocess.run(command,input=payload,capture_output=True,text=True,timeout=300,cwd=work);(work/(name+'.txt')).write_text(result.stdout);(work/(name+'-error.txt')).write_text(result.stderr);assert result.returncode==0,(name,result.stderr);outputs[name]=result.stdout.splitlines();assert len(outputs[name])==len(cases),(name,len(outputs[name]),len(cases))
differences=[dict(index=i,input=cases[i],reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(cases=len(cases),differences=len(differences),firstDifferences=differences[:30],passed=not differences,applicationParityVerified=False,scope='Actual LLLoginService constructor access block, Util.GetConfigVarFromSections<string>, and pre-account access block versus native LoginService.login with absent account. Exhaustive BMP scalar whitespace guards, ordinal identifier substring, section override and regex configuration precedence and .NET-specific pattern features. Downstream login and complete regex parity not proved.',sourceSHA256={str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [login_path,util_path,root/'OpenSim/Framework/Culture.cs',root/'bin/Nini.dll']})
(work/'results.json').write_text(json.dumps(report,indent=2));assert report['passed'],report;print(f'PASS: {len(cases)} original login access observations')

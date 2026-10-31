"""Complete original friends connector with injected request transport only."""
import argparse,hashlib,json,re,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT,extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=(ROOT/'OpenSim/Server/Base/ServerUtils.cs').read_text(encoding='utf-8-sig');parts=[extract(source,r'private static Dictionary<string, object> ScanXmlResponse\('),extract(source,r'public static Dictionary<string, object> ParseXmlResponse\(string'),extract(source,r'public static string BuildQueryString\(')]
 for name in ['ParseXmlStringResponseXmlReaderSettings','ParseXmlResponseXmlParserContext']:parts.append(re.search(r'private static readonly [^;]+ '+name+r' = [\s\S]+?;',source).group())
 wrapper=w/'ServerUtils.cs';wrapper.write_text('using System;using System.Text;using System.IO;using System.Xml;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using SWH=System.Web.HttpUtility;namespace OpenSim.Server.Base{public static class ServerUtils{static log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(ServerUtils));'+''.join(parts)+'}}',encoding='utf-8');project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 files=['OpenSim/Services/Connectors/Friends/FriendsServicesConnector.cs','OpenSim/Services/Interfaces/IFriendsService.cs','native-port/tests/friends_connector_reference.cs']
 for f in [wrapper,*[ROOT/f for f in files]]:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','OpenMetaverse','OpenMetaverse.StructuredData','Nini']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 path=w/'FriendsConnector.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 def t(s):return '-' if s is None else s.encode().hex() or '.'
 seven='00000000-0000-0000-0000-000000000007';replies=['','#throw','bad','<ServerResponse/>','<ServerResponse><result>null</result></ServerResponse>','<ServerResponse><Result>Success</Result></ServerResponse>','<ServerResponse><Result>True</Result></ServerResponse>','<ServerResponse><Result> true </Result></ServerResponse>','<ServerResponse><Result>FALSE</Result></ServerResponse>','<ServerResponse><Result type="List"><a>True</a></Result></ServerResponse>','<ServerResponse><result>True</result></ServerResponse>',f'<ServerResponse><friend0 type="List"><PrincipalID>{seven}</PrincipalID><Friend>name&amp;value</Friend><MyFlags>7</MyFlags><TheirFlags>-1</TheirFlags></friend0><other>ignored</other><friend1 type="List"/></ServerResponse>',f'<ServerResponse><friend0 type="List"><PrincipalID>bad</PrincipalID><Friend type="List"/><MyFlags>2147483648</MyFlags><TheirFlags>bad</TheirFlags></friend0></ServerResponse>','<ServerResponse><Result>&#0;true</Result></ServerResponse>','<ServerResponse><Result>True</Result></ServerResponse><broken>']
 commands=[f'{mode} {t(principal)} {t(friend)} {t(reply)}' for mode in range(5) for principal in [None,'',seven,'foreign owner & 試験'] for friend in [None,'','friend & 😃'] for reply in replies];differences=[];count=0
 for configuration in [False,True]:
  cfg=w/'friends.ini';cfg.write_text('[FriendsService]\nFriendsServerURI=http://friends.invalid/base///\n',encoding='utf-8');args=[str(cfg)] if configuration else [];rows=[]
  for command in [[a.dotnet,str(w/'app/FriendsConnector.dll'),*args],[a.native,*args]]:
   r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=45)
   if r.returncode:raise RuntimeError(r.stderr)
   rows.append(r.stdout.splitlines())
  assert len(rows[0])==len(rows[1])==len(commands)
  differences += [{'configuration':configuration,'input':c,'source':x,'native':y} for c,x,y in zip(commands,*rows) if x!=y];count+=len(commands)
 (w/'friends-connector-parity-results.json').write_text(json.dumps({'passed':not differences,'observations':count,'sourceHash':hashlib.sha256((ROOT/files[0]).read_bytes()).hexdigest(),'differences':differences},indent=2),encoding='utf-8')
 if differences:raise AssertionError(str(differences[:2]))
 print(f'PASS: {count} friends connector source observations')
if __name__=='__main__':main()

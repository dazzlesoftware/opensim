"""Compile original account handler methods, parser and XML builder."""
import argparse,hashlib,json,subprocess,urllib.parse
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_presence_endpoint import extract,ROOT
def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
    handler='OpenSim/Server/Handlers/UserAccounts/UserAccountServerPostHandler.cs';utils='OpenSim/Server/Base/ServerUtils.cs';model='OpenSim/Services/Interfaces/IUserAccountService.cs';culture='OpenSim/Framework/Culture.cs'
    source=(ROOT/handler).read_text(encoding='utf-8-sig');methods=[extract(source,r'(?:protected override|private|)\s*byte\[\] '+name+r'\(').replace('protected override','public') for name in ['ProcessRequest','GetAccount','GetAccounts','GetMultiAccounts','StoreAccount','CreateUser','FailureResult','ResultToBytes']]
    wrapper=w/'Handler.cs';wrapper.write_text('using System;using System.IO;using System.Text;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Services.Interfaces;using OpenSim.Framework;using OpenSim.Server.Base;public partial class AccountHandler {'+'\n'.join(methods)+'}',encoding='utf-8')
    source=(ROOT/utils).read_text(encoding='utf-8-sig');server=w/'ServerUtils.cs';server.write_text('using System;using System.Collections.Generic;using System.Xml;using SWH=System.Web.HttpUtility;namespace OpenSim.Server.Base{public static class ServerUtils{'+ '\n'.join(extract(source,r'(?:public|private) static [^\n{]+ '+n+r'\(') for n in ['ParseQueryString','BuildXmlResponse','BuildXmlData'])+'}}',encoding='utf-8')
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
    for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
    items=ET.SubElement(project,'ItemGroup')
    for f in [wrapper,server,ROOT/model,ROOT/culture,ROOT/'native-port/tests/account_endpoint_reference.cs']:ET.SubElement(items,'Compile',Include=str(f))
    dep=ET.SubElement(items,'Reference',Include='OpenMetaverseTypes');ET.SubElement(dep,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll')
    path=w/'AccountEndpoint.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True)
    r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    seven='00000000-0000-0000-0000-000000000007';nine=seven[:-1]+'9';zero=seven[:-1]+'0'
    bodies=['','METHOD=unknown','METHOD[]=getaccount','METHOD=getaccount','METHOD=getaccount&ScopeID=bad','METHOD=getaccount&FirstName=First&LastName=Last','METHOD=getaccount&Email=mail&FirstName=no&LastName=no']
    for key in ['UserID','PrincipalID']:
        for value in [seven,nine,zero,'bad','',seven.replace('-','')]:bodies.append('METHOD=getaccount&'+key+'='+value+'&Email=mail')
    bodies += ['METHOD=getaccount&UserID=bad&PrincipalID='+seven,'METHOD=getaccount&Email[]=mail','METHOD=getaccount&ScopeID[]='+seven+'&Email=mail']
    for query in ['', ' ', 'two','empty','nullitem','throw','試験 <&>','\u00a0two\u00a0']:
        bodies.append('METHOD=getaccounts&query='+urllib.parse.quote(query))
    bodies += ['METHOD=getaccounts','METHOD=getaccounts&query=two&ScopeID=bad','METHOD=getaccounts&query[]=two','METHOD=getmultiaccounts','METHOD=getmultiaccounts&IDS='+seven,'METHOD=getmultiaccounts&IDS[]=bad','METHOD=getmultiaccounts&IDS[]='+seven+'&IDS[]=bad&IDS[]='+zero,'METHOD=getmultiaccounts&IDS[]='+zero,'METHOD=getmultiaccounts&IDS[]='+seven+'&IDS[]='+seven,'METHOD=getmultiaccounts&IDS[]='+seven+'&ScopeID=bad']
    for extra in ['', '&FirstName=Renamed&LastName=試験&Email=&Created=-1&UserFlags=2147483647&UserTitle=%3C%26%3E','&Created=bad&UserLevel=2147483648&UserFlags=bad','&UserLevel=-99','&UserLevel=-98','&UserCountry=ignored&ServiceURLs=ignored','&UserLevel[]=1','&UserLevel=%2b12&Created=12%00']:
        bodies.append('METHOD=setaccount&PrincipalID='+seven+extra)
    bodies += ['METHOD=setaccount','METHOD=setaccount&PrincipalID='+zero,'METHOD=setaccount&PrincipalID='+nine,'METHOD=setaccount&PrincipalID='+seven+'&ScopeID=bad']
    for password in ['secret','fail','throw']:
        bodies.append('METHOD=createuser&PrincipalID='+seven+'&FirstName=New&LastName=User&Password='+password+'&Email=mail&Model=template')
    bodies += ['METHOD=createuser','METHOD=createuser&FirstName=New&LastName=User&Password=secret&PrincipalID=bad','METHOD=createuser&FirstName=New&LastName=User&Password=secret&PrincipalID='+zero]
    differences=[];count=0
    def normalized(line):
        xml,trace=line.split('|');root=ET.fromstring(bytes.fromhex(xml));
        def node(e):return [e.tag,e.attrib,'' if len(e) else (e.text or ''),[node(c) for c in e]]
        return node(root),trace
    for create,set_ in [(False,False),(True,False),(False,True),(True,True)]:
        config=w/f'config-{create}-{set_}.ini';config.write_text('[UserAccountService]\nAllowCreateUser='+str(create).lower()+'\nAllowSetAccount='+str(set_).lower()+'\n',encoding='utf-8')
        rows=[];data='\n'.join(b.encode().hex() for b in bodies)+'\n'
        for cmd in [[a.dotnet,str(w/'app/AccountEndpoint.dll'),str(config)],[a.native,str(config)]]:
            r=subprocess.run(cmd,input=data,capture_output=True,text=True,encoding='utf-8',timeout=30)
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
            rows.append(r.stdout.splitlines())
        assert len(rows[0])==len(rows[1])==len(bodies)
        for body,x,y in zip(bodies,*rows):
            count+=1
            if normalized(x)!=normalized(y):differences.append({'body':body,'create':create,'set':set_,'source':normalized(x),'native':normalized(y)})
    (w/'account-endpoint-parity-results.json').write_text(json.dumps({'passed':not differences,'observations':count,'sourceHashes':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in [handler,utils,model,culture]},'differences':differences},indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:2],indent=2))
    print(f'PASS: {count} account endpoint source observations')
if __name__=='__main__':main()

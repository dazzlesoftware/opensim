from pathlib import Path
import argparse,subprocess,socketserver,threading,socket,json,hashlib,xml.etree.ElementTree as ET,os,urllib.parse
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--nini',type=Path,required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();lab=Path(__file__).resolve().parent;w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in ['config_xml_document_network_reference.cs']:ET.SubElement(items,'Compile',Include=str(lab/f))
r=ET.SubElement(items,'Reference',Include='Nini');ET.SubElement(r,'HintPath').text=str(a.nini.resolve());cs=w/'Reference.csproj';ET.ElementTree(project).write(cs);subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'-v:q'],check=True,capture_output=True)
body="<!DOCTYPE Nini [<!ATTLIST Key Value CDATA 'Native Remote'>]><Nini><Section Name='Startup'><Key Name='Name' Value='Native Remote'/><Key Name='Greeting' Value='é中'/></Section></Nini>".encode();trace=[]
class Server(socketserver.ThreadingMixIn,socketserver.TCPServer):daemon_threads=True
class Handler(socketserver.BaseRequestHandler):
 def handle(self):
  try:
   self.request.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);data=b''
   while b'\r\n\r\n'not in data:
    b=self.request.recv(4096)
    if not b:return
    data+=b
   trace.append(data.hex());target=urllib.parse.urlsplit(data.split(b' ')[1].decode()).path.encode()
   if target==b'/start':self.request.sendall(b'HTTP/1.1 302 Found\r\nLocation: /config\r\nSet-Cookie: pipeline=yes; Path=/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
   else:
    assert b'Cookie: pipeline=yes\r\n'in data,data
    self.request.sendall(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n')
    for i in range(0,len(body),3):chunk=body[i:i+3];self.request.sendall((hex(len(chunk))[2:]+';x=y\r\n').encode()+chunk+b'\r\n')
    self.request.sendall(b'0\r\nX-End: yes\r\n\r\n')
   while self.request.recv(4096):pass
  except (ConnectionError,OSError):pass
s=Server(('127.0.0.1',0),Handler);threading.Thread(target=s.serve_forever,daemon=True).start();payload='http://127.0.0.1:'+str(s.server_address[1])+'/start\n';outputs={};traces={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll'),'--startup-reader']),('native',[str(Path(a.native).resolve())])]:
 trace.clear();r=subprocess.run(cmd,input=payload,text=True,capture_output=True,timeout=60,cwd=w);assert r.returncode==0,(name,r.stderr);outputs[name]=r.stdout;traces[name]=trace[:];(w/(name+'.txt')).write_text(r.stdout);(w/(name+'-error.txt')).write_text(r.stderr)
disabled={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll'),'--startup-reader','--disabled']),('native',[str(Path(a.native).resolve()),'--disabled'])]:
 trace.clear();r=subprocess.run(cmd,input=payload,text=True,capture_output=True,timeout=30,cwd=w);assert r.returncode==0,(name,r.stderr);assert not trace,(name,trace);assert r.stdout.startswith('E:'),(name,r.stdout);assert name!='native'or 'EXIT:1' in r.stderr,(name,r.stderr);disabled[name]={'output':r.stdout,'requests':len(trace)};(w/(name+'-disabled.txt')).write_text(r.stdout);(w/(name+'-disabled-error.txt')).write_text(r.stderr)
default_cases=[]
for label,credentials,bypass in [('environment',False,False),('credentials',True,False),('bypass',False,True)]:
 env={k:v for k,v in os.environ.items()if k.lower()not in {'http_proxy','https_proxy','all_proxy','no_proxy','gateway_interface'}}
 env['http_proxy']='http://'+('user:pass@'if credentials else '')+'127.0.0.1:'+str(s.server_address[1])
 if bypass:env['no_proxy']='127.0.0.1'
 url='http://'+('127.0.0.1:'+str(s.server_address[1])if bypass else 'startup.example:8123')+'/start\n';results={};wire={};denied={}
 for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll'),'--startup-reader','--default']),('native',[str(Path(a.native).resolve()),'--default'])]:
  trace.clear();r=subprocess.run(cmd,input=url,env=env,capture_output=True,text=True,timeout=45,cwd=w);(w/(label+'-'+name+'.txt')).write_text(r.stdout);(w/(label+'-'+name+'-error.txt')).write_text(r.stderr);assert r.returncode==0,(label,name,r.stderr);results[name]=r.stdout;wire[name]=trace[:];(w/(label+'-'+name+'-wire.json')).write_text(json.dumps(trace))
  trace.clear();r=subprocess.run(cmd+['--disabled'],input=url,env=env,capture_output=True,text=True,timeout=30,cwd=w);assert r.returncode==0 and r.stdout.startswith('E:')and not trace,(label,name,r.stdout,trace);assert name!='native'or 'EXIT:1'in r.stderr,(label,r.stderr);denied[name]=dict(output=r.stdout,requests=len(trace));(w/(label+'-'+name+'-disabled.txt')).write_text(r.stdout)
 default_cases.append(dict(case=label,output=results,wire=wire,disabled=denied))
dtd_cases=[]
for label,xml in [('ignored-default',"<!DOCTYPE Nini [<!ATTLIST Key Value CDATA 'DTD default'>]><Nini><Section Name='Startup'><Key Name='Name'/><Key Name='Greeting' Value='hi'/></Section></Nini>"),('ignored-entity',"<!DOCTYPE Nini [<!ENTITY greeting 'hello'>]><Nini><Section Name='Startup'><Key Name='Name' Value='native'/><Key Name='Greeting' Value='&greeting;'/></Section></Nini>")]:
 body=xml.encode();results={};wire={}
 for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll'),'--startup-reader']),('native',[str(Path(a.native).resolve())])]:
  trace.clear();r=subprocess.run(cmd,input=payload,capture_output=True,text=True,timeout=30,cwd=w);assert r.returncode==0,(label,name,r.stderr);results[name]=r.stdout;wire[name]=trace[:];(w/(label+'-'+name+'.txt')).write_text(r.stdout);(w/(label+'-'+name+'-error.txt')).write_text(r.stderr)
 assert results['reference']==results['native'] and results['reference'].startswith('E:') and wire['reference']==wire['native'] and len(wire['native'])==2,(label,results,wire)
 dtd_cases.append(dict(case=label,output=results,wire=wire))
s.shutdown();s.server_close();expected=''.join(f'{ord(c):04x}'for c in 'Native Remote')+':'+''.join(f'{ord(c):04x}'for c in 'é中')+'\n';report={'dtdIgnoreCases':dtd_cases,'disabledNetworking':disabled,'startupEntryPoint':True,'nativeDisabledHostExit':1,'defaultProxyCases':default_cases,'output':outputs,'wire':traces,'passed':outputs['reference']==outputs['native']==expected and traces['reference']==traces['native']and all(case['output']['reference']==case['output']['native']==expected and case['wire']['reference']==case['wire']['native']and len(case['wire']['native'])==2 and case['disabled']['reference']['output']==case['disabled']['native']['output']for case in default_cases)and disabled['reference']['output']==disabled['native']['output'],'applicationParityVerified':False,'scope':'Real direct HTTP redirect+cookie+chunk extension/trailer+UTF8+DTD Ignore+actual bundled Nini.dll XmlConfigSource(XmlReader.Create(uri, DtdProcessing.Ignore))/System.Xml versus native downloaded XML configuration source; Default environment proxy, credential-without-challenge and NO_PROXY startup composition verified; TLS/auth/pooling/default CLI and full application remain incomplete.','niniSHA256':hashlib.sha256(a.nini.read_bytes()).hexdigest()};(w/'results.json').write_text(json.dumps(report,indent=2));assert report['passed'],report;print('PASS native startup XML: legacy direct plus 3 default proxy cases; exact redirect/cookie traces and disabled networking')

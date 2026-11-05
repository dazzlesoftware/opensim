from pathlib import Path
import argparse,subprocess,json,xml.etree.ElementTree as ET
import http.server,threading
p=argparse.ArgumentParser();p.add_argument('--reference-source',type=Path);p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();d=Path(__file__).resolve().parent;w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',Nullable='enable').items():ET.SubElement(props,k).text=v
ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(a.reference_source.resolve() if a.reference_source else d/'config_http_default_proxy_reference.cs'));cs=w/'Reference.csproj';ET.ElementTree(project).write(cs)
subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'-v:q'],check=True,capture_output=True)
server=None;extra=[]
if a.reference_source:
 class Handler(http.server.BaseHTTPRequestHandler):
  def do_GET(self):
   body=b'proxy-capture';self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  def log_message(self,*args):pass
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();extra=[str(server.server_port)]
out={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 r=subprocess.run(cmd+extra,text=True,capture_output=True,timeout=45);(w/(name+'.txt')).write_text(r.stdout);(w/(name+'-error.txt')).write_text(r.stderr);assert r.returncode==0,(name,r.returncode,r.stderr);out[name]=r.stdout
if server:server.shutdown();server.server_close()
assert out['native']==out['reference'],out
(w/'results.json').write_text(json.dumps(dict(passed=True,applicationParityVerified=False,scope=('Actual HttpClient handler proxy capture across default changes and explicit proxy override; native default getter called once per handler. Authentication, OS discovery and full application remain incomplete.' if a.reference_source else 'Actual internal HttpNoProxy null/absolute/relative destinations, credential identity, HttpClient.DefaultProxy setter/getter and null rejection. Native-only injected factory priority, failure caching and concurrent setter assertions. OS proxy discovery and transport binding remain incomplete.')),indent=2))
print(out['native'],end='')

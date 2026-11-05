"""Actual XmlTextWriter/XmlDOMTextWriter state, namespace and sink differential."""
import argparse,json,random,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--reference',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
def h(s):return '~' if s is None else ''.join(f'{ord(c):04x}' for c in s) or '-'
def op(name,*values):return name+''.join(','+(str(s) if isinstance(s,int) else h(s)) for s in values)
rows=[]
def flow(commands,fail=0,dom=0,encoding=0):rows.append(f'{fail},{dom},{encoding};'+';'.join(commands))
names=[None,'','a','b','a:b',':a','a::b','a:1','a: ','xml','XML','xmlns','lang','space','a-b','d0p1','\ud800','\udc00','a\x00','\uffff']
prefixes=[None,'','p','q','xml','XML','xmlns','bad:p','\ud800']
uris=[None,'','urn:a','urn:b','http://www.w3.org/XML/1998/namespace','http://www.w3.org/2000/xmlns/','\ud800','\x00']
texts=[None,'','a','<&>"\'\n\r\t','default','preserve',' default\t','bad','--','x-','?>',']]>','\ud800','\udc00','\ud800\udc00','\uffff\x00']
for dom in [0,1]:
 for prefix in prefixes:
  for uri in uris:
   for local in ['a','xmlns','lang','space']:
    for fail in [0,2,5,8,12]:flow([op('fmt',1),op('elem',prefix,local,uri),op('attr','xml','lang',None),op('str','en'),op('attr',None,'v','urn:b'),op('qualified','n','urn:a'),'endattr',op('elem',None,'b','urn:b'),op('str','text'),'end','end','flush','close'],fail,dom)
for dom in [0,1]:
 for name in names:
  for text in texts:
   for method in ['pi','entity','name','nmtoken','comment','cdata','ws','str','raw']:
    command=op(method,name,text) if method=='pi' else op(method,text if method in ['comment','cdata','ws','str','raw'] else name)
    flow([op('doc',-1),op('elem',None,'a',None),command,'full','enddoc','close'],dom=dom)
for count in [1,8,16,17,18,22,40]:
 commands=[op('fmt',1),op('elem',None,'a','urn:a')]
 for i in range(count):commands += [op('attr','xmlns',f'p{i}',None),op('str',f'urn:{i%4}'),'endattr']
 commands += [op('elem','p0','b',None),op('attr','xmlns','p0',None),op('str','urn:new'),'endattr',op('lookup','urn:0'),op('lookup','urn:new'),'end',op('lookup','urn:0'),'end','close']
 for fail in [0,3,10,30,60,100]:flow(commands,fail)
rng=random.Random(337125)
for encoding in [1,2]:
 for value in [None,'','utf-8','native-x','\ud800','<bad>']:
  for fail in range(25):flow([op('doc',-1),op('elem',None,'a',None),'end','enddoc',op('encname',value),op('doc',1),op('elem',None,'b',None),'end','enddoc','close'],fail,encoding=encoding)
for size in [0,1,2,3,4,5,63,64,65,767,768,769,770,1536,2048]:
 payload=bytes((i*127+size)%256 for i in range(size)).hex() or '-'
 for method in ['base64','binhex']:
  for attribute in [False,True]:
   for fail in [0,2,6,8,10,12,15]:
    commands=[op('elem',None,'a',None)]+([op('attr','xml','lang',None)] if attribute else [])
    commands += [f'{method},{payload},0,{size}','base64,0123,0,1','flush',op('str',None),'base64,45,0,1',op('str',''),op('ws',''),'base64,67,0,1',op('str','text')]+(['endattr'] if attribute else [])+['end','close']
    flow(commands,fail)
for method in ['base64','binhex']:
 for payload in ['~','-','000102ff']:
  for index in [-2147483648,-1,0,1,4,5,2147483647]:
   for count in [-2147483648,-1,0,1,4,5,2147483647]:flow([op('elem',None,'a',None),'base64,01,0,1',f'{method},{payload},{index},{count}','base64,02,0,1','close'])
def command():
 method=rng.choice(['doc','enddoc','doctype','elem','end','full','attr','endattr','comment','cdata','pi','entity','char','surrogate','ws','str','raw','chars','rawchars','name','nmtoken','qualified','lookup','ns','fmt','indent','ic','quote','flush','close','base64','binhex'])
 if method in ['enddoc','end','full','endattr','flush','close']:return method
 if method=='doc':return op(method,rng.choice([-1,0,1]))
 if method in ['elem','attr']:return op(method,rng.choice(prefixes),rng.choice(names),rng.choice(uris))
 if method=='doctype':return op(method,rng.choice(names),rng.choice(texts),rng.choice(texts),rng.choice(texts))
 if method=='pi':return op(method,rng.choice(names),rng.choice(texts))
 if method=='qualified':return op(method,rng.choice(names),rng.choice(uris))
 if method=='lookup':return op(method,rng.choice(uris))
 if method in ['name','nmtoken','entity']:return op(method,rng.choice(names))
 if method in ['ns','fmt','indent']:return op(method,rng.choice([-1,0,1,2,4,65]))
 if method in ['ic','quote','char']:return op(method,rng.choice(['a',' ','\x00','"',"'",'\ud800','\udc00']))
 if method=='surrogate':return op(method,rng.choice(['\udc00\ud800','\ud800\udc00','aa','\udfff\udbff']))
 if method in ['chars','rawchars']:
  value=rng.choice(texts);size=len(value or '');return op(method,value,rng.choice([-1,0,1,size]),rng.choice([-1,0,1,size,size+1]))
 if method in ['base64','binhex']:
  payload=rng.choice(['~','-','01','000102ff','00123456789abc']);size=0 if payload in ['~','-'] else len(payload)//2;return f'{method},{payload},{rng.choice([-1,0,1,size])},{rng.choice([-1,0,1,size,size+1])}'
 return op(method,rng.choice(texts))
for _ in range(20000):flow([command() for _ in range(rng.randrange(1,22))]+['close','flush',op('str',None)],rng.randrange(36),rng.randrange(2))
callbacks=['close','end','full','endattr',op('elem','p','inner','urn:b'),op('attr','xmlns','p',None),op('doc',0),'enddoc',op('quote',"'"),op('fmt',1),op('ic','x'),op('lookup','urn:a'),op('ns',0),'flush','base64,02,0,1','binhex,03,0,1',op('str','<&'),op('raw','raw'),op('qualified','q','urn:b'),op('encname','changed')]
scenarios=[
 [op('doc',-1),op('elem',None,'a','urn:a'),op('str','text'),'full','enddoc','close'],
 [op('elem',None,'a',None),op('attr','xml','lang',None),op('str','en'),'endattr','end','close'],
 [op('elem','p','a','urn:a'),op('attr','xmlns','q',None),op('str','urn:b'),'endattr',op('elem','q','b',None),'end','end','close'],
 [op('elem',None,'a',None),'base64,01,0,1','base64,2345,0,2',op('str','x'),'end','close'],
 [op('attr',None,'a',None),op('str','x'),op('comment','ok'),'close'],
 [op('fmt',1),op('indent',130),op('elem',None,'a',None),op('elem',None,'b',None),op('elem',None,'c',None),'end','end','end','close'],
 [op('doctype','root',None,'file.dtd','<!ENTITY e "v">'),op('elem',None,'root',None),op('entity','e'),'end','close'],
]
def reentry_flow(commands,callback,trigger,fail=0,dom=0,encoding=0):rows.append(f'{fail},{dom},{encoding},{trigger};{callback};'+';'.join(commands))
for scenario in scenarios:
 for callback in callbacks:
  for trigger in range(1,23):
   for dom in [0,1]:reentry_flow(scenario,callback,trigger,dom=dom,encoding=2 if trigger%3==0 else 0)
for _ in range(4000):reentry_flow([command() for _ in range(rng.randrange(1,18))]+['close'],rng.choice(callbacks),rng.randrange(1,26),rng.randrange(26),rng.randrange(2),rng.randrange(3))
def array_flow(commands,callback='flush',trigger=0,fail=0,dom=0,mode=1):rows.append(f'{fail},{dom},0,{trigger},{mode};{callback};'+';'.join(commands))
for mode in [0,1,2]:
 for trigger in range(16):
  for callback in [op('ic','x'),op('mutate',0,'x'),op('mutate',1,'&'),'close']:
   array_flow([op('fmt',1),op('indent',130),op('elem',None,'a',None),op('elem',None,'b',None),op('elem',None,'c',None),'full','end',op('ic','q'),op('ic','r'),op('ic',' '),'end','close'],callback,trigger,mode=mode)
 for payload in ['0061003c0062','0061d800dc00','0061dc00','006100220062','006100260062']:
  for trigger in range(12):
   for callback in [op('mutate',0,'z'),op('mutate',1,'&'),op('mutate',2,'q')]:array_flow([op('elem',None,'a',None),op('attr','xml','lang',None),f'chars,{payload},0,{len(payload)//4}',op('peek',0),'endattr','end','close'],callback,trigger,mode=mode)
 array_flow([op('elem',None,'a',None),op('elem',None,'b',None),op('elem',None,'c',None),op('fmt',1),op('indent',2147483647),'full',op('indent',0),'close'],mode=mode)
 for size in [0,1,2,3,63,64,65,768,769,770,1536]:
  payload=bytes((i*197+size)%256 for i in range(size)).hex() or '-'
  array_flow([op('elem',None,'a',None),f'binhex,{payload},0,{size}','base64,01,0,1','flush','base64,23,0,1','base64,45,0,1',op('str','x'),op('peek',1000),op('mutate',1000,'q'),'base64,6789ab,0,3',op('peek',1000),'end','close'],mode=mode)
payload='\n'.join(rows)+'\n'
(a.work/'inputs.txt').write_text(payload)
def run(command):
 result=subprocess.run(command,input=payload,capture_output=True,text=True);assert result.returncode==0,result.stderr;(a.work/('managed.txt' if command[0]==a.dotnet else 'native.txt')).write_text(result.stdout);lines=result.stdout.splitlines();assert len(lines)==len(rows),(len(lines),len(rows));return lines
expected=run([a.dotnet,a.reference]);actual=run([a.native,'--probe']);differences=[{'input':rows[i],'expected':e,'actual':n} for i,(e,n) in enumerate(zip(expected,actual)) if e!=n];report={'flows':len(rows),'differences':len(differences),'examples':differences[:25],'scope':'Synchronous XmlTextWriter/XmlDOMTextWriter methods over explicit retained provider callbacks, including array identity, checked overflow, reentry and failures. File/stream encoding construction and full simulator parity remain incomplete.'};(a.work/'writer-results.json').write_text(json.dumps(report,indent=2));print(f'{len(rows)} actual writer flows; {len(differences)} differences');assert not differences,report

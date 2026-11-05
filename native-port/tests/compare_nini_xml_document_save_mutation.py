import argparse,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--native');p.add_argument('--dotnet');p.add_argument('--reference');p.add_argument('--work',type=pathlib.Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
def enc(s):return s.encode('utf-16-be','surrogatepass').hex()
xmls=['<r/>','<r></r>','<r a="first" b="second"><x>value</x><y/>tail</r>','<?xml version="1.0"?><!--before--><r a="value"><a/><b/></r><!--after-->','<r xmlns="urn:a"><a a="v"/>text<![CDATA[cdata]]></r>']
xmls.extend(['<!DOCTYPE r [<!ATTLIST r a CDATA "default">]><r><x/><y/>tail</r>','<!DOCTYPE r [<!ATTLIST r xmlns CDATA "urn:a">]><r a="first"><x/>text</r>','<!DOCTYPE r [<!ENTITY e "<x>text</x>">]><r>&e;<y/></r>','<!DOCTYPE r [<!ENTITY e "value"><!ATTLIST r a CDATA "&e;">]><r/>','<!DOCTYPE r [<!ENTITY e "value">]><r a="&e;">&e;</r>'])
rows=[f'{action} {nth} {mode} {enc(xml)}' for xml in xmls for action in range(13) for nth in range(1,101) for mode in range(2)];payload='\n'.join(rows)+'\n';(a.work/'inputs.txt').write_text(payload)
def run(cmd,name):
 r=subprocess.run(cmd,input=payload,text=True,capture_output=True);(a.work/(name+'.txt')).write_text(r.stdout);assert r.returncode==0,(r.returncode,r.stderr);v=r.stdout.splitlines();assert len(v)==len(rows),(len(v),len(rows));return v
expected=run([a.dotnet,a.reference],'managed');actual=run([a.native],'native');diff=[{'input':row,'expected':e,'actual':n}for row,e,n in zip(rows,expected,actual)if e!=n];report={'cases':len(rows),'differences':len(diff),'examples':diff[:20]};(a.work/'results.json').write_text(json.dumps(report,indent=2));print(len(rows),'live mutation cases;',len(diff),'differences');assert not diff

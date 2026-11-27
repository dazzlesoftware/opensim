"""Exact original osConsoleCommand wrapper versus native VM, with controlled hosts."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run


def build_reference(dotnet, work):
    source = ROOT/'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/OSSL_Api.cs'
    method = extract(source.read_text(encoding='utf-8-sig'), r'public bool osConsoleCommand\(')
    wrapper = '''using System;using System.Collections.Generic;using System.Text.Json;
class Reference {
enum ThreatLevel {Severe}
static bool ossl,owner,throws;
static int permissionChecks,ownerChecks;
static List<string> commands;
void CheckThreatLevel(ThreatLevel level,string function) {
 if(level!=ThreatLevel.Severe || function!="osConsoleCommand") throw new Exception("Wrong permission check");
 permissionChecks++;if(!ossl)throw new Exception("denied"); }
class Permissions {public bool CanRunConsoleCommand(int id){ownerChecks++;return owner;}}
class Scene {public Permissions Permissions=new();} Scene World=new();
class Host {public int OwnerID=42;} Host m_host=new();
class MainConsole {public static MainConsole Instance=new();
 public void RunCommand(string command){commands.Add(command);if(throws)throw new Exception("controlled");}}
METHOD
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);var f=document.RootElement;
ossl=f.GetProperty("ossl").GetBoolean();owner=f.GetProperty("owner").GetBoolean();
throws=f.GetProperty("throws").GetBoolean();permissionChecks=ownerChecks=0;commands=new();
try{bool result=new Reference().osConsoleCommand(f.GetProperty("command").GetString());
 Console.WriteLine(JsonSerializer.Serialize(new {outcome="return",result,permissionChecks,ownerChecks,commands}));
}catch(Exception){Console.WriteLine(JsonSerializer.Serialize(new {outcome="throw",permissionChecks,ownerChecks,commands}));}
}}}'''.replace('METHOD',method)
    (work/'Reference.cs').write_text(wrapper,encoding='utf-8')
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk')
    props=ET.SubElement(project,'PropertyGroup')
    for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():
        ET.SubElement(props,key).text=value
    ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include='Reference.cs')
    ET.ElementTree(project).write(work/'Reference.csproj')
    result=subprocess.run([dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    return work/'app/Reference.dll'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',required=True)
    parser.add_argument('--dotnet',required=True)
    parser.add_argument('--reference',type=Path)
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    reference=args.reference or build_reference(args.dotnet,work)
    cases=[dict(ossl=o,owner=p,throws=t,command=command)
           for o,p,t,command in itertools.product([False,True],[False,True],[False,True],
             ['', ' ', 'show regions', ' unknown command ', 'show "quoted region"', 'é😀', 'first\nsecond', 'a\0b'])]
    original=run([args.dotnet,str(reference)],cases);native=run([args.native],cases)
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,
        originalApiSourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/OSSL_Api.cs').read_bytes()).hexdigest(),
        scope='Exact original OSSL wrapper with controlled threat, owner permission and console callbacks versus native VM. Verifies ordering, unchanged commands, bool result and exception boundary; not complete permission-provider or console-command parity.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native console wrapper observations')


if __name__=='__main__':main()

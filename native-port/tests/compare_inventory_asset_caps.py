"""Exact original task script two-phase handler/updater/binary wrapper and LLSD writer."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def extract(s,sig):
 start=s.index(sig);p=s.index('{',start);depth=1;i=p+1
 # This extraction's selected methods have no braces inside string literals.
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 caps=ROOT/'OpenSim/Region/ClientStack/Linden/Caps/BunchOfCaps';update=caps/'UpdateItemAsset.cs';base=caps/'BunchOfCaps.cs';binary=ROOT/'OpenSim/Framework/Servers/HttpServer/SimpleBinaryHandler.cs';helpers=ROOT/'OpenSim/Capabilities/LLSDHelpers.cs';llsd=ROOT/'OpenSim/Capabilities/LLSD.cs'
 generated=w/'ExtractedInventoryAsset.cs';generated.write_text('using System;using System.IO;using System.Xml;using System.Reflection;using System.Collections;using System.Collections.Generic;using System.Net;using OpenMetaverse;using OpenSim.Framework.Capabilities;using OSDMap=OpenMetaverse.StructuredData.OSDMap;using OSD=OpenMetaverse.StructuredData.OSD;using Timer=ControlledTimer;\npartial class BunchOfCaps{\n'+ '\n'.join(extract(update.read_text(),s) for s in ['public void UpdateNotecardItemAsset(', 'public void UpdateAnimSetItemAsset(', 'private void UpdateInventoryItemAsset(', 'public UUID ItemUpdated(', 'static public bool ValidateAssetData(', 'public class ItemUpdater'])+'\n'+extract(base.read_text(),'public class ExpiringCapBase')+'\n}\npartial class SimpleBinaryHandler{\n'+extract(binary.read_text(),'public virtual void Handle(')+'\n}\npartial class LLSD{\n'+extract(llsd.read_text(),'public static void LLSDWriteOne(')+'\n}\nnamespace OpenSim.Framework.Capabilities { public class LLSDHelpers{\n'+extract(helpers.read_text(),'public static string SerialiseLLSDReply(')+'\n'+extract(helpers.read_text(),'private static void SerializeOSDType(')+'\n}}\n')
 originals=[ROOT/'OpenSim/Capabilities'/n for n in ['LLSDType.cs','LLSDArray.cs','LLSDAssetUploadResponse.cs','LLSDAssetUploadComplete.cs']]
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',NoWarn='0649;0169;0414').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/inventory_asset_caps_reference.cs']+originals:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'InventoryAssetReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{k} {m} {p}' for k,m,p in itertools.product([0,2],range(16),[0,6,7,8])]+[f'1 {m} {p}' for m,p in itertools.product(range(256),range(8))];rows=[]
 for cmd in [[a.dotnet,str(w/'app/InventoryAssetReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=100);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[update,base,binary,helpers,llsd]+originals+[ROOT/'bin/OpenMetaverseTypes.dll',ROOT/'bin/OpenMetaverse.StructuredData.dll']
 report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Exact extracted UpdateNotecardItemAsset, UpdateAnimSetItemAsset, UpdateInventoryItemAsset, ItemUpdated, ItemUpdater, ValidateAssetData, ExpiringCapBase and SimpleBinaryHandler.Handle; original LLSD reflection and primitive writer and actual SDK OSD conversions. Controlled route/timer/permissions/update/dump dependencies. Actual source error and completion types/masks/flags, both metadata phases, no-op wrapper, no validation/compile fields, 10MB inclusive bound, Stop before IP/delegate checks, explicit expiration/cached expired handler and backend/dump exceptions. Scene storage/owner integration is separate.',differences=diffs[:3]);(w/'inventory-asset-caps-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {len(commands)} original script task capability observations')
if __name__=='__main__':main()


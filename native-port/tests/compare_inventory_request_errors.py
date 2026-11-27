"""Original method bodies over controlled request failure versus explicit RESULT=False."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run

OPERATIONS = dict(CreateUserInventory='user', GetRootFolder='user', GetFolderForType='user, FolderType.Object',
    AddFolder='folder', UpdateFolder='folder', MoveFolder='folder',
    DeleteFolders='user, new List<UUID> { folder.ID }', PurgeFolder='folder',
    AddItem='item', UpdateItem='item', MoveItems='user, new List<InventoryItemBase> { item }',
    DeleteItems='user, new List<UUID> { item.ID }', GetAssetPermissions='user, item.AssetID')


def build_reference(dotnet, work):
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    text = source.read_text(encoding='utf-8-sig')
    methods = '\n'.join(extract(text, rf'public [^\n{{]+ {name}\(') for name in OPERATIONS)
    methods += '\n' + extract(text, r'private static bool CheckReturn\(Dictionary<string, object> ret\)')
    methods += '\n' + extract(text, r'private static InventoryFolderBase BuildFolder\(Dictionary<string,object> data\)')
    dispatch = '\n'.join(f'case "{name}": result=reference.{name}({arguments}); break;'
                         for name, arguments in OPERATIONS.items())
    wrapper = '''using System;using System.Collections.Generic;using System.Text.Json;
using OpenSim.Framework;using OpenMetaverse;using HttpUtility=System.Web.HttpUtility;
class Reference {
class Log { public void Error(object message) {} } static Log m_log=new();
class Cache { public void Remove(UUID id) {} public void AddOrUpdate(UUID id,InventoryItemBase item,double seconds) {} }
static Cache m_ItemCache=new(); const double CACHE_EXPIRATION_SECONDS=30;
bool failure; int requests;
Dictionary<string,object> Perform() { requests++; if(failure) throw new System.Net.Http.HttpRequestException("Controlled request failure"); return new() {{"RESULT","False"}}; }
Dictionary<string,object> MakeRequest(string query) => Perform();
Dictionary<string,object> MakeRequest(Dictionary<string,object> fields) => Perform();
METHODS
static void Main() {
 string line;
 while((line=Console.ReadLine())!=null) {
  using var document=JsonDocument.Parse(line);var frame=document.RootElement;
  var reference=new Reference { failure=frame.GetProperty("failure").GetBoolean() };
  var user=UUID.Zero;var folder=new InventoryFolderBase { Name="folder" };
  var item=new InventoryItemBase { Name="item",CreatorId=user.ToString() };
  try {
   object result=null;
   switch(frame.GetProperty("method").GetString()) { DISPATCH default: throw new Exception("Unknown operation"); }
   Console.WriteLine(JsonSerializer.Serialize(new { outcome="return",result,requests=reference.requests }));
  } catch(System.Net.Http.HttpRequestException) {
   Console.WriteLine(JsonSerializer.Serialize(new { outcome="throw",requests=reference.requests }));
  }
 }
}
}'''.replace('METHODS', methods).replace('DISPATCH', dispatch)
    (work / 'Reference.cs').write_text(wrapper)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in dict(TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false').items():
        ET.SubElement(props, key).text = value
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(group, 'Compile', Include='Reference.cs')
    for name in ['OpenSim.Framework', 'OpenMetaverseTypes']:
        reference = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(reference, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    path = work / 'Reference.csproj'
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)
    built = subprocess.run([dotnet, 'build', str(path), '-o', str(work / 'app'), '--nologo', '-v', 'quiet'],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return work / 'app/Reference.dll'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, work)
    libraries = {}
    for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']:
        libraries[name] = hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
        assert hashlib.sha256((reference.parent / name).read_bytes()).hexdigest() == libraries[name]
    cases = [dict(method=method, failure=failure) for method in OPERATIONS for failure in [False, True]]
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native], cases)
    differences = [dict(input=case, original=a, native=b) for case, a, b in zip(cases, original, native) if a != b]
    report = dict(passed=not differences, observations=len(cases), differences=differences, libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs').read_bytes()).hexdigest(),
        scope='13 exact original operation bodies, actual model types, controlled MakeRequest; request count and throw/return result')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native inventory request-failure and explicit-failure observations')

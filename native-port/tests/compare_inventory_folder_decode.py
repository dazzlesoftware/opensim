"""Exact original BuildFolder method and real original folder/UUID model types."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

from compare_presence_endpoint import ROOT, extract


def build_reference(dotnet, work, *, item=False, skeleton=False, single=False, collections=False, multiple_items=False):
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    model = 'InventoryItemBase' if item else 'InventoryFolderBase'
    builder = 'BuildItem' if item else 'BuildFolder'
    method = extract(source.read_text(encoding='utf-8-sig'),
                     rf'private static {model} {builder}\(Dictionary<string,object> data\)')
    wrapper = '''using System;using System.Collections.Generic;using System.Text.Json;
using OpenSim.Framework;using UUID=OpenMetaverse.UUID;
class Reference {
class Log { public void Error(object message) {} } static Log m_log=new();
METHOD
static object Decode(JsonElement value) => value.ValueKind == JsonValueKind.Object
 ? Dictionary(value) : value.GetString();
static Dictionary<string,object> Dictionary(JsonElement value) {
var result=new Dictionary<string,object>();
foreach(var item in value.EnumerateObject()) result[item.Name]=Decode(item.Value); return result; }
static void Main() { string line; while((line=Console.ReadLine())!=null) {
using var document=JsonDocument.Parse(line); var folder=BuildFolder(Dictionary(document.RootElement));
Console.WriteLine(JsonSerializer.Serialize(new { folder.Name,folder.Type,folder.Version,
ID=folder.ID.ToString(),Owner=folder.Owner.ToString(),ParentID=folder.ParentID.ToString() }));}}
}'''.replace('METHOD', method)
    if item:
        wrapper = wrapper.replace('BuildFolder(Dictionary(document.RootElement))', 'BuildItem(Dictionary(document.RootElement))')
        wrapper = wrapper.replace('folder.Name,folder.Type,folder.Version,', '''folder.Name,folder.Description,
folder.AssetType,AssetID=folder.AssetID.ToString(),folder.InvType,Folder=folder.Folder.ToString(),
folder.CreatorIdentification,folder.NextPermissions,folder.CurrentPermissions,folder.BasePermissions,
folder.EveryOnePermissions,folder.GroupPermissions,GroupID=folder.GroupID.ToString(),folder.GroupOwned,
folder.SalePrice,folder.SaleType,folder.Flags,folder.CreationDate,''')
        wrapper = wrapper.replace(',ParentID=folder.ParentID.ToString()', '')
    if skeleton:
        original = source.read_text(encoding='utf-8-sig')
        helpers = '\n'.join(extract(original, pattern) for pattern in [
            r'private static bool CheckReturn\(Dictionary<string, object> ret\)',
            r'public List<InventoryFolderBase> GetInventorySkeleton\(UUID principalID\)'])
        wrapper = wrapper[:wrapper.index('static void Main()')] + helpers + '''
Dictionary<string,object> reply;
bool transportFailure;
Dictionary<string,object> MakeRequest(string query) {
 if(transportFailure) throw new System.Net.Http.HttpRequestException("Controlled request failure");
 return reply;
}
static void Main() {
 string line;
 while((line=Console.ReadLine())!=null) {
  using var document=JsonDocument.Parse(line);
  var frame=document.RootElement;
  var reference=new Reference { reply=Dictionary(frame.GetProperty("reply")),
   transportFailure=frame.TryGetProperty("transport_failure",out var failed) && failed.GetBoolean() };
  try {
   var folders=reference.GetInventorySkeleton(UUID.Zero);
   Console.WriteLine(JsonSerializer.Serialize(new { outcome="return", folders=folders?.ConvertAll(folder=>new {
    folder.Name,folder.Type,folder.Version,ID=folder.ID.ToString(),Owner=folder.Owner.ToString(),ParentID=folder.ParentID.ToString()
   }) }));
  } catch(KeyNotFoundException) { Console.WriteLine("{\\"outcome\\":\\"missing collection\\"}"); }
    catch(InvalidCastException) { Console.WriteLine("{\\"outcome\\":\\"non-dictionary collection\\"}"); }
    catch(System.Net.Http.HttpRequestException) { Console.WriteLine("{\\"outcome\\":\\"request failure\\"}"); }
 }
}
}'''
    if single:
        original = source.read_text(encoding='utf-8-sig')
        helpers = '\n'.join(extract(original, pattern) for pattern in [
            r'private static bool CheckReturn\(Dictionary<string, object> ret\)',
            r'public InventoryFolderBase GetRootFolder\(UUID principalID\)',
            r'public InventoryFolderBase GetFolderForType\(UUID principalID, FolderType type\)',
            r'public InventoryFolderBase GetFolder\(UUID principalID, UUID folderID\)'])
        wrapper = wrapper.replace('using UUID=OpenMetaverse.UUID;',
                                  'using UUID=OpenMetaverse.UUID;using FolderType=OpenMetaverse.FolderType;')
        wrapper = wrapper[:wrapper.index('static void Main()')] + helpers + '''
Dictionary<string,object> reply;
bool transportFailure;
Dictionary<string,object> MakeRequest(string query) {
 if(transportFailure) throw new System.Net.Http.HttpRequestException("Controlled request failure");
 return reply;
}
static void Main() {
 string line;
 while((line=Console.ReadLine())!=null) {
  using var document=JsonDocument.Parse(line); var frame=document.RootElement;
  var reference=new Reference { reply=Dictionary(frame.GetProperty("reply")),
   transportFailure=frame.TryGetProperty("transport_failure",out var failed) && failed.GetBoolean() };
  try {
   var folder=frame.GetProperty("method").GetString() switch {
    "GetRootFolder"=>reference.GetRootFolder(UUID.Zero),
    "GetFolderForType"=>reference.GetFolderForType(UUID.Zero,FolderType.Object),
    _=>reference.GetFolder(UUID.Zero,UUID.Zero) };
   object value=folder==null ? null : new { folder.Name,folder.Type,folder.Version,
    ID=folder.ID.ToString(),Owner=folder.Owner.ToString(),ParentID=folder.ParentID.ToString() };
   Console.WriteLine(JsonSerializer.Serialize(new { outcome="return",folder=value }));
  } catch(KeyNotFoundException) { Console.WriteLine("{\\"outcome\\":\\"missing collection\\"}"); }
    catch(InvalidCastException) { Console.WriteLine("{\\"outcome\\":\\"non-dictionary collection\\"}"); }
    catch(System.Net.Http.HttpRequestException) { Console.WriteLine("{\\"outcome\\":\\"request failure\\"}"); }
 }
}
}'''
    if collections:
        original = source.read_text(encoding='utf-8-sig')
        helpers = '\n'.join(extract(original, pattern) for pattern in [
            r'private static bool CheckReturn\(Dictionary<string, object> ret\)',
            r'private static InventoryItemBase BuildItem\(Dictionary<string,object> data\)',
            r'public InventoryCollection GetFolderContent\(UUID principalID, UUID folderID\)',
            r'public virtual InventoryCollection\[\] GetMultipleFoldersContent\(UUID principalID, UUID\[\] folderIDs\)'])
        wrapper = wrapper.replace('public void Error(object message) {}',
            'public void Error(object message) {} public void Warn(object message) {} public void WarnFormat(string format,params object[] args) {}')
        wrapper = wrapper[:wrapper.index('static void Main()')] + helpers + '''
Dictionary<string,object> reply;
bool transportFailure;
Dictionary<string,object> MakeRequest(string query) {
 if(transportFailure) throw new System.Net.Http.HttpRequestException("Controlled request failure");
 return reply;
}
static object Project(InventoryCollection value) => value==null ? null : new {
 Owner=value.OwnerID.ToString(),Folder=value.FolderID.ToString(),Version=value.Version,
 Folders=value.Folders.ConvertAll(folder=>new {folder.Name,folder.Type,folder.Version,
  ID=folder.ID.ToString(),Owner=folder.Owner.ToString(),ParentID=folder.ParentID.ToString()}),
 Items=value.Items.ConvertAll(item=>new {item.Name,ID=item.ID.ToString(),Owner=item.Owner.ToString()}) };
static void Main() {
 string line;
 while((line=Console.ReadLine())!=null) {
  using var document=JsonDocument.Parse(line);var frame=document.RootElement;
  var reference=new Reference {reply=Dictionary(frame.GetProperty("reply")),
   transportFailure=frame.TryGetProperty("transport_failure",out var failed) && failed.GetBoolean()};
  var ids=new List<UUID>();foreach(var id in frame.GetProperty("ids").EnumerateArray()) ids.Add(new UUID(id.GetString()));
  var owner=new UUID("22222222-2222-4222-8222-222222222222");
  object result;
  if(frame.GetProperty("method").GetString()=="single") result=Project(reference.GetFolderContent(owner,ids[0]));
  else { var values=reference.GetMultipleFoldersContent(owner,ids.ToArray());
   result=values==null ? null : Array.ConvertAll(values,Project); }
  Console.WriteLine(JsonSerializer.Serialize(new {result}));
 }
}
}'''
    if multiple_items:
        original = source.read_text(encoding='utf-8-sig')
        helpers = '\n'.join(extract(original, pattern) for pattern in [
            r'private static bool CheckReturn\(Dictionary<string, object> ret\)',
            r'public InventoryItemBase GetItem\(UUID principalID, UUID itemID\)',
            r'public virtual InventoryItemBase\[\] GetMultipleItems\(UUID principalID, UUID\[\] itemIDs\)'])
        wrapper = wrapper.replace('using System;', 'using System;using System.Text;', 1)
        wrapper = wrapper.replace('public void Error(object message) {}',
            'public void Error(object message) {} public void WarnFormat(string format,params object[] args) {}')
        wrapper = wrapper[:wrapper.index('static void Main()')] + helpers + '''
static ExpiringCacheOS<UUID,InventoryItemBase> m_ItemCache=new(15000);
const double CACHE_EXPIRATION_SECONDS=30;
Dictionary<string,object> reply;
bool failure;
int requests;
Dictionary<string,object> MakeRequest(string query) {
 requests++;
 if(failure) throw new System.Net.Http.HttpRequestException("Controlled request failure");
 return reply;
}
static object Project(InventoryItemBase item) => item==null ? null : new {
 item.Name,ID=item.ID.ToString(),Owner=item.Owner.ToString() };
static void Main() {
 string line;
 while((line=Console.ReadLine())!=null) {
  using var document=JsonDocument.Parse(line);var frame=document.RootElement;
  m_ItemCache.Clear();
  var owner=new UUID("22222222-2222-4222-8222-222222222222");
  foreach(var seed in frame.GetProperty("cache").EnumerateArray()) {
   var id=new UUID(seed.GetProperty("id").GetString());
   InventoryItemBase item=null;
   if(!seed.TryGetProperty("null",out var nil) || !nil.GetBoolean())
    item=new InventoryItemBase {ID=id,Owner=owner,Name=seed.GetProperty("name").GetString()};
   m_ItemCache.AddOrUpdate(id,item,CACHE_EXPIRATION_SECONDS);
  }
  var steps=new List<object>();
  foreach(var step in frame.GetProperty("steps").EnumerateArray()) {
   var reference=new Reference {reply=Dictionary(step.GetProperty("reply")),
    failure=step.TryGetProperty("failure",out var failed) && failed.GetBoolean()};
   var ids=new List<UUID>();foreach(var id in step.GetProperty("ids").EnumerateArray()) ids.Add(new UUID(id.GetString()));
   object result;
   if(step.TryGetProperty("method",out var method) && method.GetString()=="single")
    result=Project(reference.GetItem(owner,ids[0]));
   else { var values=reference.GetMultipleItems(owner,ids.ToArray());
    result=values==null ? null : Array.ConvertAll(values,Project); }
   steps.Add(new {result,requests=reference.requests});
  }
  var inspected=new List<object>();
  foreach(var raw in frame.GetProperty("inspect").EnumerateArray()) {
   bool hit=m_ItemCache.TryGetValue(new UUID(raw.GetString()),out var item);
   inspected.Add(new {hit,item=Project(item)});
  }
  Console.WriteLine(JsonSerializer.Serialize(new {steps,cache=inspected}));
 }
 m_ItemCache.Dispose();
}
}'''
    (work / 'Reference.cs').write_text(wrapper, encoding='utf-8')
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
    result = subprocess.run([dotnet, 'build', str(path), '-o', str(work / 'app'), '--nologo', '-v', 'quiet'],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return work / 'app/Reference.dll'


def run(command, cases):
    result = subprocess.run(command, input=''.join(json.dumps(case) + '\n' for case in cases),
                            text=True, encoding='utf-8', capture_output=True, timeout=40)
    assert result.returncode == 0, result.stdout + result.stderr
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == len(cases), (len(rows), len(cases), result.stderr)
    return rows


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
    for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']:
        assert hashlib.sha256((reference.parent / name).read_bytes()).digest() == \
            hashlib.sha256((ROOT / 'bin' / name).read_bytes()).digest(), f'Original model library mismatch: {name}'
    folder = dict(Name='Folder & café', Type='8', Version='17',
        ID='11111111-1111-4111-8111-111111111111', Owner='22222222-2222-4222-8222-222222222222',
        ParentID='33333333-3333-4333-8333-333333333333')
    cases = [folder, {}]
    for key in folder:
        cases.append({name: value for name, value in folder.items() if name != key})
        cases.append({**folder, key: dict(nested='value')})
    for key in ['Type', 'Version']:
        for value in ['', '+8', '-0', '  +8  ', '\t8\t', '1.0', 'junk', '-32769', '-32768', '-1',
                      '32767', '32768', '65535', '65536', '2147483648', '4294967295', '\u00a08\u00a0']:
            cases.append({**folder, key: value})
    for key in ['ID', 'Owner', 'ParentID']:
        for value in ['', 'junk', '11111111111141118111111111111111', ' 11111111-1111-4111-8111-111111111111 ']:
            cases.append({**folder, key: value})
    cases += [{**folder, 'Name': ''}, {**folder, 'extra': 'ignored'}]
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native], cases)
    differences = [dict(input=case, original=a, native=b)
                   for case, a, b in zip(cases, original, native) if a != b]
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    report = dict(passed=not differences, observations=len(cases), differences=differences,
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        scope='Exact extracted original BuildFolder method, original InventoryFolderBase and SDK UUID types',
        libraries={name: hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
                   for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']})
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native folder decoding observations')

"""Compare osFormatString's exact source wrapper and actual LSLString boxes."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run


def build_reference(dotnet, work):
    source = ROOT / 'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/OSSL_Api.cs'
    method = extract(source.read_text(encoding='utf-8-sig'), r'public LSL_String osFormatString\(')
    wrapper = '''using System;using System.Linq;using System.Text.Json;
using OpenSim.Region.ScriptEngine.Shared;
using LSL_String=OpenSim.Region.ScriptEngine.Shared.LSL_Types.LSLString;
using LSL_List=OpenSim.Region.ScriptEngine.Shared.LSL_Types.list;
class Reference {
enum ThreatLevel {VeryLow} void CheckThreatLevel(ThreatLevel level,string function) {}
METHOD
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);var frame=document.RootElement;
try {var values=frame.TryGetProperty("mixed",out var mixed) && mixed.GetBoolean()
 ? new object[]{new LSL_Types.LSLInteger(12),new LSL_String("é😀"),new LSL_Types.LSLFloat(1.25),
     new LSL_Types.Vector3(1,2,3),new LSL_Types.Quaternion(0,0,0,1)}
 : frame.GetProperty("arguments").EnumerateArray().Select(x=>(object)new LSL_String(x.GetString())).ToArray();
var list=new LSL_List(values);
var result=new Reference().osFormatString(frame.GetProperty("format").GetString(),list).ToString();
Console.WriteLine(JsonSerializer.Serialize(new {outcome="return",result}));
}catch(Exception){Console.WriteLine("{\\"outcome\\":\\"throw\\"}");}
}}}'''.replace('METHOD', method)
    (work / 'Reference.cs').write_text(wrapper, encoding='utf-8')
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in dict(TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false').items():
        ET.SubElement(props, key).text = value
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(group, 'Compile', Include='Reference.cs')
    for name in ['OpenSim.Region.ScriptEngine.Shared', 'OpenMetaverse', 'OpenMetaverseTypes']:
        reference = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(reference, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    ET.ElementTree(project).write(work / 'Reference.csproj')
    result = subprocess.run([dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'),
                             '--nologo', '-v', 'quiet'], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return work / 'app/Reference.dll'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve(); work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, work)
    library = 'OpenSim.Region.ScriptEngine.Shared.dll'
    assert (reference.parent / library).read_bytes() == (ROOT / 'bin' / library).read_bytes()
    formats = ['', 'plain', '{{}}', '{{{0}}}', '{0}', '{1}', '{2}', '{0}{1}{0}',
               '{', '}', '{{', '}}', '{0:{{}}}', '{0:}}}', '{0:}', '{0:+#,0.00}',
               '{00}', '{00000000000000000000}', '{0,00000000}', '{0,-0}',
               '{10000000}', '{0,10000000}', '{0,1000000}', '{0,9999999}']
    # Bound output sizes while checking malformed oversized field grammar.
    formats.remove('{0,1000000}'); formats.remove('{0,9999999}')
    for index, alignment, spec in itertools.product(['0','1','2',' 0','+0','-1','0 ','0\t','٠'],
            ['', ',0', ',4', ',-4', ', 8 ', ',+4', ', - 4', ',\t4', ',4x'],
            ['', ':', ':F2', ':X', ':ignored', ':{', ':a:b']):
        formats.append('before{' + index + alignment + spec + '}after')
    rng = random.Random(4231)
    formats += [''.join(rng.choices('{}0, :-ax', k=rng.randrange(1,25))) for _ in range(600)]
    cases = [dict(format=fmt, arguments=values) for fmt in formats
             for values in [[], ['x'], ['', 'é😀'], ['A\0B', 'second', '第三']]]
    mixed_cases = [dict(format='{' + str(index) + alignment + spec + '}', mixed=True)
                   for index, alignment, spec in itertools.product(range(6), ['', ',25', ',-25'], ['', ':F2', ':X'])]
    cases += mixed_cases
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native], cases)
    differences = [dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report = dict(passed=not differences,observations=len(cases),mixedVmObservations=len(mixed_cases),differences=differences,
        originalApiSourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/OSSL_Api.cs').read_bytes()).hexdigest(),
        lslAssemblySha256=hashlib.sha256((ROOT/'bin'/library).read_bytes()).hexdigest(),
        scope='Exact OSSL wrapper using actual LSL types: composite grammar, return/throw, UTF16 padding; 54 mixed native VM/source observations for integers, strings, floats, vectors and rotations. Permission checks are separate native tests. Exception messages/types, huge allocations, lone surrogates and all numeric conversion/culture extremes are not proven.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native format observations')


if __name__ == '__main__':
    main()

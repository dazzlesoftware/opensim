"""Exact Util AES helpers with actual SDK byte encoding and cross-decryption."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run


def build_reference(dotnet, work):
    source = (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig')
    names = ['AESEncrypt', 'AESEncryptTo', 'AESEncryptString', 'AESDecrypt', 'AESDecryptFrom', 'AESDecryptString']
    methods = '\n'.join(extract(source, r'(?:public|private) static (?:string|ReadOnlySpan<char>) ' + name + r'\(')
                        for name in names)
    wrapper = '''using System;using System.IO;using System.Text;using System.Text.Json;
using System.Security.Cryptography;using OpenMetaverse;
class Reference {
METHODS
static void Main() { string line;while((line=Console.ReadLine())!=null) {
 using var document=JsonDocument.Parse(line);var frame=document.RootElement;
 var secret=frame.GetProperty("secret").GetString();var text=frame.GetProperty("text").GetString();
 var iv=frame.TryGetProperty("iv",out var value) ? value.GetString() : "";
 try { var result=frame.GetProperty("op").GetString()=="sdkbytes" ? Convert.ToHexString(Utils.StringToBytesNoTerm(text.AsSpan()))
  : frame.GetProperty("op").GetString()=="encrypt"
  ? AESEncryptTo(secret.AsSpan(),text.AsSpan(),iv.AsSpan())
  : AESDecryptFrom(secret.AsSpan(),text.AsSpan(),iv.AsSpan()).ToString();
  Console.WriteLine(JsonSerializer.Serialize(new {outcome="return",result}));
 } catch(Exception) {Console.WriteLine("{\\"outcome\\":\\"throw\\"}");}
}}
}'''.replace('METHODS', methods)
    (work / 'Reference.cs').write_text(wrapper)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in dict(TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false').items():
        ET.SubElement(props, key).text = value
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(group, 'Compile', Include='Reference.cs')
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        reference = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(reference, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    path = work / 'Reference.csproj'
    ET.ElementTree(project).write(path)
    built = subprocess.run([dotnet, 'build', str(path), '-o', str(work / 'app'), '--nologo', '-v', 'quiet'],
                           text=True, capture_output=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return work / 'app/Reference.dll'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, work)
    libraries = {}
    for name in ['OpenMetaverse.dll', 'OpenMetaverseTypes.dll']:
        libraries[name] = hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
        assert hashlib.sha256((reference.parent / name).read_bytes()).hexdigest() == libraries[name]
    original_command, native_command = [args.dotnet, str(reference)], [args.native]
    cases = [dict(op='encrypt', secret=secret, text=text, iv=iv)
        for secret in ['', 'secret', 'café 😀', 'embedded\0key', 'a\0é', '\0leading', 'trailing\0']
        for text in ['', 'hello', 'café 😀', '\ufeffhello', 'embedded\0text'] + ['x' * n for n in [15, 16, 17, 31, 32, 33]]
        for iv in ['iv', 'café 😀', 'embedded\0iv', 'é\0😀', '\0', 'iv\0']]
    original = run(original_command, cases)
    native = run(native_command, cases)
    all_cases, all_original, all_native = list(cases), list(original), list(native)
    decrypt = []
    for case, result in zip(cases, original):
        if result['outcome'] == 'return' and result['result']:
            for iv in ['', case['iv']]:
                decrypt.append(dict(op='decrypt', secret=case['secret'], text=result['result'], iv=iv))
            decrypt.append(dict(op='decrypt', secret=case['secret'],
                                text='ignored:' + result['result'].split(':')[1], iv=case['iv']))
    for text in ['', 'missing separator', ':', 'gg:00', '0:00', '00:gg', '00:0', '00:',
                 '0' * 32 + ':00', '0' * 32 + ':' + '0' * 32, ' 00:00', '0x00:00']:
        for secret in ['', 'secret']:
            for iv in ['', 'iv']:
                decrypt.append(dict(op='decrypt', secret=secret, text=text, iv=iv))
    all_cases += decrypt
    all_original += run(original_command, decrypt)
    all_native += run(native_command, decrypt)
    differences = [dict(input=case, original=a, native=b)
                   for case, a, b in zip(all_cases, all_original, all_native) if a != b]
    random_checks = []
    for source, target in [(native_command, original_command), (original_command, native_command)]:
        requests = [dict(op='encrypt', secret='secret', text=text, iv='') for text in ['random payload', 'café 😀']]
        encrypted = run(source, requests)
        recovered = run(target, [dict(op='decrypt', secret='secret', text=row['result'], iv='') for row in encrypted])
        for request, row, decoded in zip(requests, encrypted, recovered):
            iv, payload = row['result'].split(':')
            assert len(iv) == 32 and iv == iv.upper() and payload == payload.lower()
            assert decoded == dict(outcome='return', result=request['text'])
            random_checks.append(True)
    report = dict(passed=not differences, observations=len(all_cases), differences=differences,
        crossRuntimeRandomIvChecks=len(random_checks), libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT / 'OpenSim/Framework/Util.cs').read_bytes()).hexdigest(),
        scope='Exact original six Util AES methods and actual SDK StringToBytesNoTerm/UUID; deterministic ciphertext, decrypt return/throw and four bidirectional random-IV decrypt checks. Exception runtime types/messages, lone UTF16 surrogates and every arbitrary decrypted byte stream are outside this corpus.')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(all_cases)} original/native AES observations and {len(random_checks)} random-IV cross-runtime checks')

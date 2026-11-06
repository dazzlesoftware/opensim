"""Run source account conversion and generic Store through bundled SQLite."""

import argparse, hashlib, json, platform, shutil, subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_presence_endpoint import extract, ROOT


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--source-provider', action='store_true')
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    service = 'OpenSim/Services/UserAccountService/UserAccountService.cs'
    table = 'OpenSim/Data/SQLite/SQLiteGenericTableHandler.cs'
    methods = [
        extract(
            (ROOT / service).read_text(encoding='utf-8-sig'),
            r'(?:private|public) [^\n{]+ ' + n + r'\(',
        )
        for n in ['MakeUserAccount', 'StoreUserAccount']
    ]
    store = extract((ROOT / table).read_text(encoding='utf-8-sig'), r'public virtual bool Store\(')
    methods += [
        extract((ROOT / service).read_text(encoding='utf-8-sig'), pattern)
        for pattern in [
            r'public UserAccount GetUserAccount\(UUID scopeID, string email\)',
            r'public UserAccount GetUserAccount\(UUID scopeID, UUID principalID\)',
            r'public UserAccount GetUserAccount\(UUID scopeID, string firstName,\s*string lastName\)',
        ]
    ]
    table_methods = [
        extract((ROOT / table).read_text(encoding='utf-8-sig'), pattern)
        for pattern in [
            r'public virtual T\[\] Get\(string\[\] fields, string\[\] keys\)',
            r'protected T\[\] DoQuery\(',
            r'private void CheckColumnNames\(',
        ]
    ]
    methods.append(
        extract(
            (ROOT / service).read_text(encoding='utf-8-sig'),
            r'public List<UserAccount> GetUserAccounts\(UUID scopeID, string query\)',
        )
    )
    search_source = 'OpenSim/Data/SQLite/SQLiteUserAccountData.cs'
    table_methods.append(
        extract(
            (ROOT / search_source).read_text(encoding='utf-8-sig'),
            r'public UserAccountData\[\] GetUsers\(',
        ).replace('UserAccountData[] GetUsers', 'T[] GetUsers')
    )
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using System;using System.Text;using System.Collections.Generic;using System.Reflection;using System.Data;using System.Data.SQLite;using OpenMetaverse;using OpenSim.Data;using OpenSim.Framework;using OpenSim.Services.Interfaces;public partial class AccountRecordReference {'
        + '\n'.join(methods)
        + '}public partial class OriginalTable<T> where T:class,new(){'
        + store
        + '\n'.join(table_methods)
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    items = ET.SubElement(project, 'ItemGroup')
    sources = [
        'OpenSim/Services/Interfaces/IUserAccountService.cs',
        'OpenSim/Data/IUserAccountData.cs',
        'OpenSim/Framework/Culture.cs',
        'native-port/tests/account_record_reference.cs',
    ]
    for f in [wrapper, *(ROOT / s for s in sources)]:
        ET.SubElement(items, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'System.Data.SQLite']:
        item = ET.SubElement(items, 'Reference', Include=name)
        ET.SubElement(item, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'AccountRecord.csproj'
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)
    built = subprocess.run(
        [
            a.dotnet,
            'build',
            str(path),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    system = platform.system()
    arm = platform.machine().lower() in ['arm64', 'aarch64']
    source, target = (
        {
            'Windows': ('e_sqlite3.dll', 'e_sqlite3.dll'),
            'Linux': ('libe_sqlite3-arm64.so' if arm else 'libe_sqlite3.so', 'libe_sqlite3.so'),
            'Darwin': (
                'libe_sqlite3_OSX_arm64.dylib' if arm else 'libe_sqlite3_OSX_x64.dylib',
                'libe_sqlite3.dylib',
            ),
        }
    )[system]
    shutil.copy2(ROOT / 'bin/lib64' / source, w / 'app' / target)
    rows = []
    for cmd in [
        [a.dotnet, str(w / 'app/AccountRecord.dll')],
        [a.native] + (['--source'] if a.source_provider else []),
    ]:
        r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', timeout=30)
        if r.returncode:
            raise RuntimeError(r.stdout + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == 124
    differences = [{'source': x, 'native': y} for x, y in zip(*rows) if x != y]
    (w / 'account-record-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': 124,
                'nativeSourceProvider': a.source_provider,
                'sourceHashes': {
                    s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest()
                    for s in [service, table, search_source, *sources[:3]]
                },
                'differences': differences,
            },
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    if differences:
        raise AssertionError(differences)
    print('PASS: 124 account record SQLite provider observations')


if __name__ == '__main__':
    main()

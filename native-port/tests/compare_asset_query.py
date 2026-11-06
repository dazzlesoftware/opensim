"""Original OSHttpRequest.BuildQueryDictionary, managed Uri and HttpUtility."""

import argparse, hashlib, itertools, json, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Framework/Servers/HttpServer/OSHttpRequest.cs'
    text = source.read_text(encoding='utf-8-sig')
    start = text.index('private void BuildQueryDictionary()')
    opening = text.index('{', start)
    end = opening + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    (w / 'Reference.cs').write_text(
        'using System;using System.Text;using System.Web;using System.Globalization;using System.Collections.Generic;using System.Collections.Specialized;class Request{public NameValueCollection QueryString;}class Reference{Request m_request;Dictionary<string,string> _queryAsDictionay;HashSet<string> m_queryFlags;'
        + text[start:end]
        + '''static string H(string s)=>s==null?"N":Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){try{var target=Encoding.UTF8.GetString(Convert.FromHexString(line));var uri=new Uri("http://127.0.0.1"+target);var r=new Reference{m_request=new Request{QueryString=HttpUtility.ParseQueryString(uri.Query)}};r.BuildQueryDictionary();foreach(var item in r._queryAsDictionay)Console.Write(H(item.Key)+":"+H(item.Value)+";");Console.WriteLine();}catch{Console.WriteLine("EX");}}}}''',
        encoding='utf-8',
    )
    (w / 'Reference.csproj').write_text(
        '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>',
        encoding='utf-8',
    )
    r = subprocess.run(
        [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    words = [
        "Σ",
        "σ",
        "ς",
        "I",
        "i",
        "İ",
        "ı",
        "ſ",
        "S",
        "K",
        "K",
        "𐐀",
        "𐐨",
        '',
        'texture_id',
        'TEXTURE_ID',
        'mesh_id',
        'Mesh_id',
        'é',
        'e\u0301',
        'a',
        'A',
        'ａ',
        'a\u200b',
        'a\0',
        '%u0061',
        '%61',
        '%',
        '%x1',
        '%C0%AF',
        '%ED%A0%80',
        'a+b',
        'a%26b',
        'a=b',
        'a#b',
    ]
    cases = (
        [
            '/asset',
            '/asset?',
            '/asset?flag&=value',
            '/asset?x=1&&y=2&',
            '/asset?texture_id=1&TEXTURE_ID=2',
        ]
        + [
            '/asset?' + left + '=one&' + right + '=two'
            for left, right in itertools.product(words, words)
        ]
        + ['/asset?x=' + value for value in words]
    )
    commands = '\n'.join(c.encode().hex() for c in cases) + '\n'
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/Reference.dll')], [a.native]]:
        r = subprocess.run(cmd, input=commands, capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    differences = [
        {'target': c, 'source': s, 'native': n} for c, s, n in zip(cases, *rows) if s != n
    ]
    (w / 'asset-query-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(cases),
                'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Original dictionary builder; managed Uri.Query and HttpUtility; controlled request facade. General Uri parsing/platform/culture/invalid UTF16 remain pending.',
                'differences': differences[:15],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:3]
    print(f'PASS: {len(cases)} asset query source observations')


if __name__ == '__main__':
    main()

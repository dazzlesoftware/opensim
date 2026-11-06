import argparse, pathlib, subprocess, json

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', default='dotnet')
p.add_argument('--work-dir', '--work', dest='work_dir', required=True)
a = p.parse_args()
w = pathlib.Path(a.work_dir).resolve()
w.mkdir(parents=True, exist_ok=True)
s = pathlib.Path(__file__).with_name('nini_xml_file_input_reference.cs')
(w / 'Program.cs').write_bytes(s.read_bytes())
(w / 'reference.csproj').write_text(
    '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><RuntimeFrameworkVersion>8.0.31</RuntimeFrameworkVersion><RollForward>Disable</RollForward></PropertyGroup></Project>'
)
subprocess.run(
    [a.dotnet, 'build', str(w / 'reference.csproj'), '-c', 'Release', '--nologo', '-v:q'],
    check=True,
)
paths = []
for length in [0, 1, 2, 3, 4, 7, 31, 4095, 4096, 4097, 8193, 65536, 262145]:
    f = w / f'body-{length}.bin'
    f.write_bytes(bytes((i * 193 + 37) % 256 for i in range(length)))
    paths.append(f)
for name in ['a space %.xml', '\u00e9\u4e2d\u03a9.xml', 'name#query.xml', 'trailing.', 'trailing ']:
    f = w / name
    try:
        f.write_bytes(b'<a/>')
        paths.append(f)
    except OSError:
        pass
long = w / ('a' * 110) / ('b' * 110) / ('c' * 80)
long.mkdir(parents=True, exist_ok=True)
f = long / 'long.xml'
f.write_bytes(b'<a/>')
paths.append(f)
paths.extend([w / 'missing.bin', w / 'missing-dir' / 'missing.bin', w])
held_handle = None
if __import__('os').name == 'nt':
    import ctypes

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [
        ctypes.c_wchar_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_void_p,
    ]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    held = w / 'sharing-held.bin'
    held.write_bytes(b'<a/>')
    held_handle = kernel.CreateFileW(str(held), 0x80000000, 0, None, 3, 0, None)
    if held_handle == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error())
    paths.append(held)
count = 0
for i, f in enumerate(paths):
    expected = subprocess.check_output(
        [a.dotnet, str(w / 'bin/Release/net8.0/reference.dll'), str(f)], text=True
    )
    actual = subprocess.check_output([str(pathlib.Path(a.native).resolve()), str(f)], text=True)
    (w / f'{i}-reference.txt').write_text(expected)
    (w / f'{i}-native.txt').write_text(actual)
    if expected != actual:
        print('MISMATCH', ascii(str(f)))
        print(expected[:3000])
        print(actual[:3000])
        raise SystemExit(1)
    count += len(expected.splitlines())
if held_handle is not None:
    kernel.CloseHandle(held_handle)
report = {
    'cases': count,
    'files': len(paths),
    'equal': True,
    'platform': 'actual host; not a Unix/macOS execution claim',
}
(w / 'report.json').write_text(json.dumps(report, indent=2))
print(report)

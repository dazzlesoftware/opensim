"""Pinned BCL oracle; all registry writes are within one validated GUID-owned HKCU subtree."""

import pathlib, subprocess, os, json, struct, winreg, uuid, ctypes, datetime, argparse, hashlib

parser = argparse.ArgumentParser()
parser.add_argument('--native', required=True)
parser.add_argument('--dotnet', required=True)
parser.add_argument('--reference', required=True)
parser.add_argument('--work', type=pathlib.Path, required=True)
cli = parser.parse_args()
p = cli.work.resolve()
p.mkdir(parents=True, exist_ok=True)
prefix = 'Software\\OpenSimNativeTimeZoneTests\\'
root = prefix + uuid.uuid4().hex
hive = 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones'
fixtures = {}


def tzi(bias=300, std=0, dst=-60, sd=(0, 11, 0, 1, 2, 0, 0, 0), dd=(0, 3, 0, 2, 2, 0, 0, 0)):
    return struct.pack('<iii16H', bias, std, dst, *sd, *dd)


def item(name, data=None, kind=winreg.REG_BINARY, dynamic=None):
    fixtures[name] = dict(
        raw=isinstance(data, bytes),
        kind=kind,
        data=data.hex() if isinstance(data, bytes) else data,
        dynamic=dynamic,
    )


item(
    'Marker',
    tzi(bias=0, sd=(0,) * 8, dd=(0,) * 8),
    dynamic={
        'FirstEntry': (2000, winreg.REG_DWORD),
        'LastEntry': (2000, winreg.REG_DWORD),
        '2000': (tzi(), winreg.REG_BINARY),
    },
)
item('Marker\uffff', tzi(bias=0, sd=(0,) * 8, dd=(0,) * 8))
item('Milliseconds', tzi(sd=(0, 11, 0, 1, 2, 0, 0, 500), dd=(0, 3, 0, 2, 2, 0, 0, 500)))
item('Denied', tzi())
item('Valid', tzi())
item('Raw\ud800', tzi())
item('Raw\ud801', tzi(bias=0))
item('Raw\udc00', tzi())
item('Raw\ufffd', tzi())
item('Deseret_\U00010428', tzi())
item('Fixed', tzi(sd=(0,) * 8, dd=(0,) * 8))
item('MissingTZI')
item('WrongSize', b'123')
item('StringTZI', 'garbage', winreg.REG_SZ)
item('NoneTZI', tzi(), winreg.REG_NONE)
item('BE_TZI', tzi(), winreg.REG_DWORD_BIG_ENDIAN)
item('BadBias', tzi(bias=10000))
item('CombinedHigh', tzi(bias=-840, dst=-60))
item('CombinedLow', tzi(bias=840, dst=60))
item('BadDelta', tzi(dst=10000))
item('SameDates', tzi(dd=(0, 11, 0, 1, 2, 0, 0, 0)))
for index, name in enumerate(
    ['Year', 'Month', 'Weekday', 'Day', 'Hour', 'Minute', 'Second', 'Millisecond']
):
    values = [0, 3, 0, 2, 2, 0, 0, 0]
    values[index] = [65535, 13, 7, 6, 24, 60, 60, 1000][index]
    item('Bad' + name, tzi(dd=tuple(values)))
for index, name in enumerate(
    ['Year', 'Month', 'Weekday', 'Day', 'Hour', 'Minute', 'Second', 'Millisecond']
):
    values = [2024, 3, 0, 2, 2, 0, 0, 0]
    values[index] = [65535, 0, 65535, 0, 24, 60, 60, 1000][index]
    item('FixedBad' + name, tzi(dd=tuple(values)))
for name, dyn in [
    ('NoEntries', {}),
    ('MissingLast', {'FirstEntry': (2000, winreg.REG_DWORD)}),
    ('Backwards', {'FirstEntry': (2001, winreg.REG_DWORD), 'LastEntry': (2000, winreg.REG_DWORD)}),
    ('FirstString', {'FirstEntry': ('2000', winreg.REG_SZ), 'LastEntry': (2001, winreg.REG_DWORD)}),
    ('FirstQword', {'FirstEntry': (2000, winreg.REG_QWORD), 'LastEntry': (2001, winreg.REG_DWORD)}),
    (
        'FirstBytes',
        {'FirstEntry': (b'1234', winreg.REG_BINARY), 'LastEntry': (2001, winreg.REG_DWORD)},
    ),
    (
        'MissingYear',
        {'FirstEntry': (2000, winreg.REG_DWORD), 'LastEntry': (2001, winreg.REG_DWORD)},
    ),
    (
        'Year0',
        {
            'FirstEntry': (0, winreg.REG_DWORD),
            'LastEntry': (1, winreg.REG_DWORD),
            '0': (tzi(), winreg.REG_BINARY),
            '1': (tzi(), winreg.REG_BINARY),
        },
    ),
    (
        'Year10000',
        {
            'FirstEntry': (9999, winreg.REG_DWORD),
            'LastEntry': (10000, winreg.REG_DWORD),
            '9999': (tzi(), winreg.REG_BINARY),
            '10000': (tzi(), winreg.REG_BINARY),
        },
    ),
    (
        'DynamicValid',
        {
            'FirstEntry': (2000, winreg.REG_DWORD),
            'LastEntry': (2001, winreg.REG_DWORD),
            '2000': (tzi(), winreg.REG_BINARY),
            '2001': (tzi(), winreg.REG_BINARY),
        },
    ),
]:
    item(name, tzi(), dynamic=dyn)
for a, b in __import__('itertools').product(
    [-2147483648, -2147483647, -10000, -841, -840, -1, 0, 1, 840, 841, 10000, 2147483647], repeat=2
):
    item('Bounds_' + str(a) + '_' + str(b), tzi(bias=a, dst=b))
    item('FixedBounds_' + str(a) + '_' + str(b), tzi(bias=a, dst=b, sd=(0,) * 8, dd=(0,) * 8))
for kind in range(13):
    for size in [0, 1, 2, 3, 4, 5, 7, 8, 9, 16, 43, 44, 45, 48]:
        data = (tzi() + bytes(48))[:size]
        item('TziRaw_' + str(kind) + '_' + str(size), data, kind)
    for size in [0, 1, 2, 3, 4, 5, 7, 8, 9, 16]:
        data = (struct.pack('<I', 2000) + bytes(16))[:size]
        item(
            'IntRaw_' + str(kind) + '_' + str(size),
            tzi(),
            dynamic={
                'FirstEntry': (data, kind),
                'LastEntry': (2000, winreg.REG_DWORD),
                '2000': (tzi(), winreg.REG_BINARY),
            },
        )


def set_value(key, name, value, kind):
    if isinstance(value, bytes):
        dll = ctypes.WinDLL('advapi32')
        dll.RegSetValueExW.argtypes = [
            ctypes.c_void_p,
            ctypes.c_wchar_p,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.c_uint32,
        ]
        buffer = ctypes.create_string_buffer(value)
        status = dll.RegSetValueExW(int(key), name, 0, kind, buffer, len(value))
        assert status == 0, (kind, len(value), status)
    else:
        winreg.SetValueEx(
            key,
            name,
            0,
            kind,
            (
                (value & 0xFFFFFFFF)
                if kind == winreg.REG_DWORD and isinstance(value, int)
                else (
                    (value & 0xFFFFFFFFFFFFFFFF)
                    if kind == winreg.REG_QWORD and isinstance(value, int)
                    else value
                )
            ),
        )


for year in [-2147483648, -2, -1, 0, 1, 9999, 10000, 2147483647]:
    item(
        'Single_' + str(year),
        tzi(),
        dynamic={
            'FirstEntry': (year, winreg.REG_DWORD),
            'LastEntry': (year, winreg.REG_DWORD),
            str(year): (tzi(), winreg.REG_BINARY),
        },
    )


def cleanup(path):
    assert path == root or path.startswith(root + '\\')
    try:
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_READ | winreg.KEY_WRITE
        ) as key:
            while True:
                try:
                    child = winreg.EnumKey(key, 0)
                except OSError:
                    break
                cleanup(path + '\\' + child)
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
    except FileNotFoundError:
        pass


restore = []
try:
    for name, value in fixtures.items():
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, root + '\\' + hive + '\\' + name) as key:
            if value['data'] is not None:
                set_value(
                    key,
                    'TZI',
                    bytes.fromhex(value['data']) if value['raw'] else value['data'],
                    value['kind'],
                )
        if value['dynamic'] is not None:
            with winreg.CreateKey(
                winreg.HKEY_CURRENT_USER, root + '\\' + hive + '\\' + name + '\\Dynamic DST'
            ) as key:
                for k, (v, kind) in value['dynamic'].items():
                    set_value(key, k, v, kind)
    # Child-only denied-read case; retain an already-open owner handle and restore exact DACL.
    adv = ctypes.WinDLL('advapi32', use_last_error=True)
    adv.RegGetKeySecurity.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    adv.RegSetKeySecurity.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p]
    adv.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [
        ctypes.c_wchar_p,
        ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ctypes.c_uint32),
    ]
    ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    held = winreg.OpenKey(
        winreg.HKEY_CURRENT_USER, root + '\\' + hive + '\\Denied', 0, winreg.KEY_ALL_ACCESS
    )
    size = ctypes.c_uint32()
    adv.RegGetKeySecurity(int(held), 4, None, ctypes.byref(size))
    original = ctypes.create_string_buffer(size.value)
    assert adv.RegGetKeySecurity(int(held), 4, original, ctypes.byref(size)) == 0
    restore.append((held, original))
    descriptor = ctypes.c_void_p()
    assert adv.ConvertStringSecurityDescriptorToSecurityDescriptorW(
        'D:P(D;;KR;;;WD)', 1, ctypes.byref(descriptor), None
    )
    assert adv.RegSetKeySecurity(int(held), 4, descriptor) == 0
    ctypes.windll.kernel32.LocalFree(descriptor)
    ids = (
        list(fixtures)
        + [
            '\\Valid',
            '\\\\Valid',
            'Valid\\\\',
            'Marker\uffff\\',
            '\\Marker\uffff',
            'DynamicValid\\\\Dynamic DST\\',
        ]
        + ['RAW\ud800', 'Raw\ud801', 'DESERET_\U00010400']
        + ['Unknown', '\\', 'Valid\\', 'Valid\\Dynamic DST', 'DynamicValid\\Dynamic DST']
    )
    enc = lambda s: ''.join(
        format(int.from_bytes(s.encode('utf-16-le', 'surrogatepass')[i : i + 2], 'little'), '04x')
        for i in range(0, len(s.encode('utf-16-le', 'surrogatepass')), 2)
    )
    rows = '\n'.join(map(enc, ids)) + '\n'
    outputs = {}
    query_labels = list(ids)
    for instant in [datetime.datetime(2024, 3, 10, 7), datetime.datetime(2024, 11, 3, 6)]:
        ticks = (
            (instant - datetime.datetime(1, 1, 1)).days * 864000000000
            + (instant.hour * 3600 + instant.minute * 60 + instant.second) * 10000000
            + 5000000
        )
        for delta in [-1, 0, 1]:
            rows += enc('Milliseconds') + '#' + str(ticks + delta) + '\n'
            query_labels.append('Milliseconds#' + str(ticks + delta))
    ids = query_labels
    profiles = []
    for label, inv, nls in [('default', '0', '0'), ('invariant', '1', '0'), ('nls', '0', '1')]:
        env = os.environ.copy()
        env.update(
            DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=inv, DOTNET_SYSTEM_GLOBALIZATION_USENLS=nls
        )
        sourcecmd = [cli.dotnet, cli.reference]
        metadata = json.loads(
            subprocess.run(
                sourcecmd + ['--runtime'], env=env, text=True, capture_output=True, check=True
            ).stdout
        )
        assert metadata['runtime'] == '8.0.31'
        nativeargs = (
            ['--invariant'] if metadata['invariant'] else ['--nls'] if metadata['nls'] else []
        )
        for kind, cmd in [
            ('source', sourcecmd),
            ('native', [cli.native] + nativeargs + ['--probe']),
        ]:
            outputs[kind] = subprocess.run(
                cmd + ['--registry', root],
                input=rows,
                env=env,
                text=True,
                capture_output=True,
                check=True,
            ).stdout.splitlines()
        assert len(outputs['source']) == len(outputs['native']) == len(ids)
        profiles.append(
            dict(
                profile=label,
                metadata=metadata,
                observations=[
                    dict(id=i, source=s, native=n)
                    for i, s, n in zip(ids, outputs['source'], outputs['native'])
                ],
            )
        )

    subprocess.run(
        [cli.native, '--provider-units', '--registry', root], check=True, capture_output=True
    )

    def serial(v):
        if isinstance(v, bytes):
            return {'bytes': v.hex()}
        raise TypeError()

    report = dict(
        scope='Actual Windows owned HKCU registry values/permissions with child-only HKLM override; no actual HKLM writes. All GUID-owned keys and DACLs are restored/removed.',
        root=root,
        fixtures=fixtures,
        profiles=profiles,
        observations=profiles[0]['observations'],
    )
    (p / 'time-zone-lookup-registry-results.json').write_text(
        json.dumps(report, indent=2, default=serial), encoding='utf-8'
    )
    diff = [
        dict(profile=profile['profile'], **r)
        for profile in profiles
        for r in profile['observations']
        if r['source'] != r['native']
    ]
    print('registry cases', len(ids) * len(profiles), 'failures', len(diff))
    assert not diff, json.dumps(diff[:5], indent=2)
finally:
    for held, descriptor in restore:
        assert adv.RegSetKeySecurity(int(held), 4, descriptor) == 0
        held.Close()
    cleanup(root)


try:
    winreg.OpenKey(winreg.HKEY_CURRENT_USER, root).Close()
    raise AssertionError('Owned fixture registry root survived cleanup')
except FileNotFoundError:
    pass
report['ownedGuidSubtreeRemoved'] = True
report['nativeSha256'] = hashlib.sha256(pathlib.Path(cli.native).read_bytes()).hexdigest()
(p / 'time-zone-lookup-registry-results.json').write_text(
    json.dumps(report, indent=2, default=serial), encoding='utf-8'
)

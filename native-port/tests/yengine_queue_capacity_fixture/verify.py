"""Independently validate genuine original queued XML capacity observations."""
import datetime
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET


BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify():
    report = {
        'schemaVersion': 1,
        'checkedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'passed': True,
        'fullParityVerified': False,
        'scope': 'Actual unchanged original PostEvent admission and production queue XML object writer/reader at List65537/100000 and raw/SDK String4194305, Windows/Linux x64',
        'sourceCompiledOrDispatched': False,
        'listArgumentTypedEventDispatchClaimed': False,
        'methodology': 'Queue event nodes follow XMRInstCapture production loop. Payload XML comes only from unchanged private AppendXMLObjectArray and AppendXMLDetectArray; unchanged RestoreEventQueue parses it. Python independently parses each XML item/value and hashes complete cell contents.',
        'generatorSha256': digest(BASE / 'Reference.cs'),
        'generatorAssemblySha256': digest(BASE / 'bin/Release/net8.0/Reference.dll'),
        'platforms': {},
    }
    platform_rows = {}
    for platform in ['windows', 'linux']:
        metadata = BASE / platform / 'metadata.json'
        rows = json.loads(metadata.read_text())
        assert len(rows) == 4
        observations = []
        for row in rows:
            folder = BASE / platform / row['name']
            assert row['postAccepted'] and row['queuedCount'] == row['restoredCount'] == 1
            assert row['before'] == row['after']
            assert row['generatorSha256'] == report['generatorSha256']
            assert row['originalAssemblySha256'] == digest(ROOT / 'bin/OpenSim.Region.ScriptEngine.YEngine.dll')
            assert row['originalSharedAssemblySha256'] == digest(ROOT / 'bin/OpenSim.Region.ScriptEngine.Shared.dll')
            for name, expected in row['originalSourceSha256'].items():
                assert digest(ROOT / 'OpenSim/Region/ScriptEngine/YEngine' / name) == expected
            xml_path = folder / 'event-queue.xml'
            assert digest(xml_path) == row['xmlSha256']
            xml_bytes = xml_path.read_bytes()
            assert len(xml_bytes) == row['xmlBytes']
            xml = ET.fromstring(xml_bytes)
            events = xml.findall('Event')
            assert len(events) == 1 and events[0].get('Name') == row['eventName']
            parameters = events[0].findall('param')
            assert len(parameters) == row['parameterCount']
            payload = parameters[-1]
            length = row['length']
            if row['kind'] == 'list':
                assert payload.get('type') == 'list'
                items = payload.findall('item')
                assert len(items) == length
                actual_hash = hashlib.sha256()
                for index, item in enumerate(items):
                    assert item.get('type') == 'OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLInteger'
                    number = int(item.text)
                    assert number == (-7 if index == length - 1 else 19)
                    actual_hash.update(struct.pack('<i', number))
                expected_hash = hashlib.sha256(struct.pack('<i', 19) * (length - 1) + struct.pack('<i', -7)).hexdigest()
                assert actual_hash.hexdigest() == expected_hash
            else:
                assert payload.get('type') == row['before']['clr']
                value = payload.text
                expected = bytearray(b'x' * length)
                expected[0] = ord('A')
                expected[length // 2] = ord('B')
                expected[-1] = ord('Z')
                assert len(value) == length and value.encode('utf-8') == expected
                expected_hash = hashlib.sha256(expected).hexdigest()
            assert row['before']['cellsSha256'] == expected_hash
            observations.append({
                'name': row['name'], 'kind': row['kind'], 'length': length,
                'fullCellsSha256': expected_hash, 'xmlBytes': len(xml_bytes),
                'xmlSha256': digest(xml_path), 'metadataSha256': digest(folder / 'metadata.json'),
                'postAcceptedWrittenRestored': True,
                'originalClrTypePreserved': row['before']['clr'],
            })
        platform_rows[platform] = observations
        report['platforms'][platform] = {
            'metadataSha256': digest(metadata), 'runtime': rows[0]['runtime'],
            'architecture': rows[0]['architecture'], 'processId': rows[0]['processId'],
            'cases': observations,
        }
    for windows, linux in zip(platform_rows['windows'], platform_rows['linux']):
        assert {key: value for key, value in windows.items() if key != 'metadataSha256'} == {
            key: value for key, value in linux.items() if key != 'metadataSha256'
        }
    report['crossPlatformXmlAndContentIdentical'] = True
    report['verifierSha256'] = digest(Path(__file__))
    destination = BASE / 'verification.json'
    assert not destination.exists()
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n')
    print('PASS 8 original queue capacity observations:', digest(destination))


if __name__ == '__main__':
    verify()

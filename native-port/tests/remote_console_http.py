# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
import argparse, subprocess, http.client, xml.etree.ElementTree as ET, urllib.parse, time, concurrent.futures

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
a = p.parse_args()
proc = subprocess.Popen(
    [a.native], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
)
checks = 0
try:
    line = proc.stdout.readline().strip()
    assert line.startswith('LISTENING '), line
    port = int(line.split()[1])

    def call(path, body='', method='POST', connection=None):
        global checks
        c = http.client.HTTPConnection('127.0.0.1', port, timeout=35)
        try:
            headers = {'Content-Type': 'application/x-www-form-urlencoded'}
            if connection:
                headers['Connection'] = connection
            c.request(method, path, body, headers)
            r = c.getresponse()
            data = r.read()
            checks += 1
            return r.status, dict((k.lower(), v) for k, v in r.getheaders()), data
        finally:
            c.close()

    status, headers, data = call('/StartSession', 'USER=user&PASS=wrong')
    assert status == 401 and not data and 'access-control-allow-origin' not in headers
    status, headers, data = call('/StartSession', 'USER=user&PASS=wrong', connection='close')
    assert status == 401 and headers['connection'] == 'close'
    ids = []
    for encoding in ['utf-8-sig', 'utf-16', 'utf-16-be', 'utf-32', 'utf-32-be']:
        body = 'USER=user&PASS=pass'.encode(encoding)
        if encoding.endswith('-be'):
            body = (b'\xfe\xff' if encoding == 'utf-16-be' else b'\x00\x00\xfe\xff') + body
        status, headers, data = call('/StartSession', body, 'PUT')
        assert (
            status == 200
            and headers.get('access-control-allow-origin') == 'https://console.example'
        ), (encoding, status, headers, data)
        root = ET.fromstring(data)
        assert (
            root.findtext('Prompt') in ('Region', 'Region# ') and root.find('HelpTree') is not None
        )
        ids.append(root.findtext('SessionID'))
    session = ids[0]
    path = '/ReadResponses/' + session
    status, headers, data = call(path)
    assert (
        status == 200
        and headers['connection'] == 'close'
        and ET.fromstring(data).find('Line') is not None
    )
    with concurrent.futures.ThreadPoolExecutor() as pool:
        waiting = pool.submit(call, path)
        time.sleep(0.15)
        assert not waiting.done()
        status, _, data = call(
            '/SessionCommand',
            urllib.parse.urlencode({'ID': session, 'COMMAND': 'echo "hello world"'}),
        )
        assert status == 200 and ET.fromstring(data).findtext('Result') == 'OK'
        status, _, data = waiting.result()
        assert status == 200
        text = data.decode()
        for _ in range(6):
            if 'hello world' in text and 'Level="info"' in text:
                break
            text += call(path)[2].decode()
        assert 'hello world' in text and 'Level="info"' in text, text
    for event, level in [
        ('error', 'error'),
        ('warn', 'warn'),
        ('fatal', 'normal'),
        ('debug', 'normal'),
    ]:
        assert call('/FixtureLog', event)[0] == 200
        status, _, data = call(path)
        lines = ET.fromstring(data).findall('Line')
        assert status == 200 and any(
            line.get('Level') == level and line.text == 'LOG {0}: ' + event + '\n' for line in lines
        ), (event, data)
    for route in ['/FixtureInventory', '/FixtureLibrary']:
        owner = '00000000-0000-0000-0000-000000000200'
        folder = '00000000-0000-0000-0000-000000000001'
        request = (
            '<llsd><map><key>folders</key><array><map><key>folder_id</key><uuid>'
            + folder
            + '</uuid><key>owner_id</key><uuid>'
            + owner
            + '</uuid></map></array></map></llsd>'
        )
        status, _, data = call(route, request)
        assert status == 200 and folder.encode() in data
        status, _, data = call(path)
        expected = (
            '[WEB FETCH INV DESC HANDLER]: Unable to fetch folders owned by '
            + owner
            + ' : '
            + folder
        )
        assert status == 200 and any(
            line.get('Level') == 'warn' and line.text == expected
            for line in ET.fromstring(data).findall('Line')
        ), (route, data)
        assert call(route, 'bad')[0] == 200
        status, _, data = call(path)
        prefix = '[FETCH INV DESC]' if route.endswith('Inventory') else '[FETCH LIB DESC]'
        assert status == 200 and any(
            line.get('Level') == 'error'
            and line.text.startswith(prefix + ': fail parsing request: Unable to cast object')
            for line in ET.fromstring(data).findall('Line')
        ), (route, data)
    assert call('/SessionCommand', 'ID=bad&COMMAND=x')[0] == 404
    assert call('/sTaRtSeSsIoN/extra?x=y', 'USER=user&PASS=wrong')[0] == 401
    assert call('/StartSessionExtra', 'USER=user&PASS=wrong')[0] == 401
    for session in ids:
        assert call('/CloseSession', 'ID=' + session)[0] == 200
    assert call('/ReadResponses/' + ids[0])[0] == 200
    print(
        'PASS: remote console HTTP authentication, BOM decoding, command wake, XML output, poll and close;',
        checks,
        'requests',
    )
finally:
    if proc.poll() is None:
        proc.stdin.write('stop\n')
        proc.stdin.flush()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        raise
    assert proc.returncode == 0, proc.stderr.read()

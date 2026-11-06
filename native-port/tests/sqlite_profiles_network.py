"""Exercise durable profile operations through the production native executable."""

from pathlib import Path
import contextlib
import argparse, subprocess, tempfile, json, sqlite3, http.client, queue, threading, re, uuid

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--local-region', action='store_true')
p.add_argument('--requests-probe')
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
executable = str(Path(a.native).resolve())
checks = []


def check(value, label):
    assert value, label
    checks.append(label)


with tempfile.TemporaryDirectory(prefix='profiles-', dir=a.work.resolve()) as temp:
    root = Path(temp)
    config = root / 'server.ini'
    database = root / 'profiles.db'
    settings = f'''[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=accounts.db
AuthenticationDatabase=accounts.db
[Network]
http_listener_port=0
[DatabaseService]
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=Data Source={database}
[UserAccountService]
AllowCreateUser=true
[UserProfilesService]
Enabled=true
LocalServiceModule=OpenSim.Services.UserProfilesService.dll:UserProfilesService
UserAccountService=OpenSim.Services.UserAccountService.dll:UserAccountService
'''
    if a.local_region:
        settings = (
            settings.replace(
                '[NativeServer]',
                '[NativeServer]\nEnableLocalRegion=true\nInventoryDatabase=inventory.db\nGridUserDatabase=gridusers.db',
            )
            + '\n[NativeRegion]\nUDPPort=0\nBindAddress=127.0.0.1\nExternalHost=127.0.0.1\n[Modules]\nUserProfilesServices=LocalUserProfilesServicesConnector\n'
        )
    config.write_text(settings, encoding='utf-8')
    for options, password in [
        (['--init-db'], None),
        (['--create-user', 'Profile', 'User'], 'profile-password\n'),
    ]:
        r = subprocess.run(
            [executable, '--config', str(config), *options],
            input=password,
            capture_output=True,
            text=True,
            timeout=30,
        )
        check(r.returncode == 0, 'CLI ' + str(options) + ': ' + r.stderr)
    with contextlib.closing(sqlite3.connect(root / 'accounts.db')) as db, db:
        user = db.execute(
            "select PrincipalID from UserAccounts where FirstName='Profile'"
        ).fetchone()[0]
        db.execute(
            "update UserAccounts set Email='account@example.test' where PrincipalID=?", (user,)
        )
    classified = str(uuid.uuid4())
    pick = str(uuid.uuid4())
    snapshot = str(uuid.uuid4())
    target = str(uuid.uuid4())
    process = None
    connection = None

    def stop():
        global process, connection
        if connection:
            connection.close()
            connection = None
        if process and process.poll() is None:
            process.stdin.write('quit\n')
            process.stdin.flush()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise
            check(process.returncode == 0, 'server graceful shutdown')

    def start():
        global process, connection
        process = subprocess.Popen(
            [executable, '--config', str(config), '--console'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding='utf-8',
        )
        output = queue.Queue()

        def drain():
            for line in process.stdout:
                output.put(line)

        threading.Thread(target=drain, daemon=True).start()
        lines = []
        while True:
            try:
                line = output.get(timeout=25)
            except queue.Empty:
                raise AssertionError('server did not start: ' + ''.join(lines))
            lines.append(line)
            match = re.search(r'LISTENING http://127\.0\.0\.1:(\d+)/', line)
            if match:
                connection = http.client.HTTPConnection('127.0.0.1', int(match[1]), timeout=15)
                break
        check(process.poll() is None, 'profile-enabled executable listening')

    def rpc(method, params={}):
        body = json.dumps(
            dict(jsonrpc='2.0', id='test', method=method, params=params), separators=(',', ':')
        )
        connection.request('POST', '/profiles', body, {'Content-Type': 'application/json-rpc'})
        response = connection.getresponse()
        raw = response.read()
        check(response.status == 200, method + ' HTTP200')
        value = json.loads(raw)
        check(value.get('id') == 'test' and value.get('jsonrpc') == '2.0', method + ' envelope')
        return value

    def result(method, params={}):
        value = rpc(method, params)
        check('error' not in value, method + ' successful')
        return value.get('result')

    try:
        start()
        check(database.exists(), 'profile service creates SQLite database on enabled startup')
        with contextlib.closing(sqlite3.connect(database)) as db, db:
            check(
                db.execute("select version from migrations where name='UserProfiles'").fetchone()
                == (5,),
                'original profile migrations stamped at5',
            )
            db.execute('insert into usersettings values(?,?,?,?)', (user, 0, 0, 'old@example.test'))
            db.execute('insert into userdata values(?,?,?,?)', (user, 'tag', 'key', 'stored'))
        ad = result(
            'classified_update',
            dict(
                ClassifiedId=classified,
                CreatorId=user,
                Name='ad é',
                Price=100,
                SnapshotId=snapshot,
                Flags=0,
            ),
        )
        check(
            ad['Flags'] == 4
            and ad['ParcelName'] == 'Unknown'
            and ad['Description'] == 'No Description',
            'classified maturity and storage defaults',
        )
        check(
            any(
                x['classifieduuid'] == classified
                for x in result('avatarclassifiedsrequest', dict(creatorId=user))
            ),
            'classified list reads persisted record',
        )
        check(
            result('classifieds_info_query', dict(ClassifiedId=classified))['Name'] == 'ad é',
            'classified info reads persisted data',
        )
        error = rpc('classified_delete', dict(classifiedId=classified))
        check(
            error['error']['code'] == -32604,
            'original classified delete parameter failure retained',
        )
        result(
            'picks_update',
            dict(
                PickId=pick,
                CreatorId=user,
                Name='pick',
                TopPick=True,
                Enabled=True,
                SnapshotId=snapshot,
            ),
        )
        check(
            any(x['pickuuid'] == pick for x in result('avatarpicksrequest', dict(creatorId=user))),
            'pick list reads persisted record',
        )
        selected = result('pickinforequest', dict(PickId=pick, CreatorId=user))
        check(
            not selected.get('TopPick', False) and not selected.get('Enabled', False),
            'original numeric boolean pick persistence retained',
        )
        result('avatar_notes_update', dict(UserId=user, TargetId=target, Notes='durable note'))
        check(
            result('avatarnotesrequest', dict(UserId=user, TargetId=target))['Notes']
            == 'durable note',
            'notes round trip',
        )
        preference = result(
            'user_preferences_update', dict(UserId=user, IMViaEmail=True, Visible=True)
        )
        check(
            preference['EMail'] == 'account@example.test',
            'local account identity adapter fills missing preference email',
        )
        error = rpc('user_preferences_request', dict(UserId=user))
        check(
            error['error']['code'] == -32604
            and 'Insufficient parameters' in error['error']['message'],
            'original preference read parameter error retained',
        )
        if a.requests_probe:
            request_target = str(uuid.uuid4())
            r = subprocess.run(
                [
                    str(Path(a.requests_probe).resolve()),
                    '--network',
                    f'http://127.0.0.1:{connection.port}/profiles',
                    user,
                    request_target,
                ],
                capture_output=True,
                text=True,
                encoding='utf-8',
                timeout=60,
            )
            check(r.returncode == 0, 'actual controller RPC composition: ' + r.stdout + r.stderr)
            check(
                r.stdout.count('PASS:') == 18,
                'eighteen actual controller route/reply/cache scenarios',
            )
            with contextlib.closing(sqlite3.connect(database)) as db:
                controller_note = db.execute(
                    'select notes from usernotes where useruuid=? and targetuuid=?',
                    (user, request_target),
                ).fetchone()
                check(
                    controller_note == ('controller note 😀 <&',),
                    'controller notes persisted by production SQLite provider: '
                    + repr(controller_note)
                    + ' codepoints '
                    + repr([hex(ord(c)) for c in (controller_note or [''])[0]]),
                )
                check(
                    db.execute(
                        'select imviaemail,visible,email from usersettings where useruuid=?',
                        (user,),
                    ).fetchone()
                    == (1, 0, 'account@example.test'),
                    'controller preference update persisted with actual account email fallback',
                )
                check(
                    db.execute('select name from userpicks where pickuuid=?', (pick,)).fetchone()
                    == ('pick',),
                    'picks cache reference mutation leaves persistent title unchanged',
                )
                check(
                    db.execute(
                        'select pickuuid from userpicks where pickuuid=?', (request_target,)
                    ).fetchone()
                    is None,
                    'controller pick delete removes temporary pick from production SQLite',
                )
                check(
                    db.execute(
                        'select profileWantToMask,profileSkillsMask,profileWantToText,profileSkillsText,profileLanguages from userprofile where useruuid=?',
                        (user,),
                    ).fetchone()
                    == (-1, -2147483648, 'controller wants 😀', 'controller skills', 'en fr'),
                    'async interest update persists signed source masks and Unicode',
                )
                check(
                    db.execute(
                        'select profileAboutText,profileFirstText,profileURL,profileImage,profileFirstImage from userprofile where useruuid=?',
                        (user,),
                    ).fetchone()
                    == (
                        'controller about 😀',
                        'controller first life',
                        '',
                        '00000000-0000-0000-0000-000000000047',
                        '00000000-0000-0000-0000-000000000048',
                    ),
                    'async properties update persists web policy and original image fields',
                )
                controller_pick = '00000000-0000-0000-0000-00000000aa55'
                parcel = str(
                    uuid.UUID(
                        bytes=((512 << 32) | 768).to_bytes(8, 'little')
                        + (10).to_bytes(2, 'little')
                        + bytes(2)
                        + (20).to_bytes(2, 'little')
                        + bytes(2)
                    )
                )
                check(
                    db.execute(
                        'select name,description,user,simname,posglobal,gatekeeper,sortorder,toppick,enabled,parceluuid from userpicks where pickuuid=?',
                        (controller_pick,),
                    ).fetchone()
                    == (
                        'new controller pick',
                        'new controller description',
                        'native parcel',
                        'native region',
                        '<522, 788, 30>',
                        'http://native.example',
                        7,
                        0,
                        0,
                        parcel,
                    ),
                    'controller pick creation persists independent scene parcel and request fields',
                )
                check(
                    db.execute(
                        'select name from classifieds where classifieduuid=?', (classified,)
                    ).fetchone()
                    == ('ad é',),
                    'classified cache reference mutation leaves persistent title unchanged',
                )
                check(
                    db.execute(
                        'select description,category,parentestate,parceluuid,parcelname,simname,posglobal from classifieds where classifieduuid=?',
                        (classified,),
                    ).fetchone()
                    == (
                        'native controller description',
                        '8',
                        4,
                        '00000000-0000-0000-0000-000000000006',
                        'native parcel',
                        'native region',
                        '<12.5, -4, 256>',
                    ),
                    'classified update persists actual scene parcel and request fields',
                )
        data = result('user_data_request', dict(UserId=user, TagId='tag'))
        check(data['result'] == 'success', 'application data read through source response shape')
        result(
            'user_data_update', dict(UserId=user, TagId='tag', DataKey='new', DataVal='new value')
        )
        result('avatar_properties_request', dict(UserId=user))
        result(
            'avatar_properties_update',
            dict(
                UserId=user,
                WebUrl='http://profile.test/',
                AboutText='about',
                ImageId=snapshot,
                FirstLifeImageId=snapshot,
            ),
        )
        result(
            'avatar_interests_update',
            dict(
                UserId=user,
                WantToMask=17,
                WantToText='wants',
                SkillsMask=3,
                SkillsText='skills',
                Language='en',
            ),
        )
        props = result('avatar_properties_request', dict(UserId=user))
        check(
            props['WebUrl'] == 'http://profile.test/' and props['WantToMask'] == 17,
            'properties and interests persist through service',
        )
        check(
            result('image_assets_request', dict(avatarId=user)) == [snapshot] * 4,
            'profile image assets include classified pick and profile images',
        )
        stop()
        with contextlib.closing(sqlite3.connect(database)) as db, db:
            check(
                db.execute(
                    'select DataVal from userdata where UserId=? and TagId=?', (user, 'tag')
                ).fetchone()
                == ('stored',),
                'service application data update remains original no-op',
            )
            check(
                db.execute('select email from usersettings where useruuid=?', (user,)).fetchone()
                == ('account@example.test',),
                'preference update written to SQLite',
            )
        start()
        check(
            result('avatarnotesrequest', dict(UserId=user, TargetId=target))['Notes']
            == 'durable note',
            'notes survive process restart',
        )
        check(
            result('avatar_properties_request', dict(UserId=user))['WantToMask'] == 17,
            'properties survive process restart',
        )
        if a.requests_probe:
            result('picks_delete', dict(pickId=controller_pick))
        result('picks_delete', dict(pickId=pick))
        check(result('avatarpicksrequest', dict(creatorId=user)) == [], 'pick delete persists')
        result('avatar_notes_update', dict(UserId=user, TargetId=target, Notes=''))
        check(
            'Notes' not in result('avatarnotesrequest', dict(UserId=user, TargetId=target)),
            'empty notes remove record',
        )
        stop()
        config.write_text(
            settings.replace('Enabled=true', 'Enabled=false').replace(
                'OpenSim.Services.UserProfilesService.dll:UserProfilesService',
                'unsupported-disabled-module',
            ),
            encoding='utf-8',
        )
        start()
        check(
            rpc('avatarclassifiedsrequest', dict(creatorId=user))['error']['code'] == -32600,
            'disabled connector registers no profile handlers',
        )
        stop()
        if a.local_region:
            config.write_text(
                settings.replace(
                    'UserProfilesServices=LocalUserProfilesServicesConnector',
                    'UserProfilesServices=Other',
                ),
                encoding='utf-8',
            )
            start()
            check(
                rpc('avatarclassifiedsrequest', dict(creatorId=user))['error']['code'] == -32600,
                'unselected local connector registers no handlers',
            )
            stop()
            config.write_text(
                settings.replace(
                    'LocalServiceModule=OpenSim.Services.UserProfilesService.dll:UserProfilesService',
                    'LocalServiceModule=',
                ),
                encoding='utf-8',
            )
            start()
            check(
                rpc('avatarclassifiedsrequest', dict(creatorId=user))['error']['code'] == -32600,
                'empty local module logs and keeps region runnable',
            )
            stop()
            config.write_text(
                settings.replace(
                    'LocalServiceModule=OpenSim.Services.UserProfilesService.dll:UserProfilesService',
                    'LocalServiceModule=MissingProfile.dll:Missing',
                ),
                encoding='utf-8',
            )
            start()
            check(
                rpc('avatarclassifiedsrequest', dict(creatorId=user))['error']['code'] == -32600,
                'failed local service load logs and keeps region runnable',
            )
            stop()
            invalid_profile_storage = settings.replace(
                '[UserProfilesService]', '[UserProfilesService]\nStorageProvider=MissingStorage.dll'
            )
            config.write_text(invalid_profile_storage, encoding='utf-8')
            start()
            check(
                rpc('avatarclassifiedsrequest', dict(creatorId=user))['error']['code'] == -32600,
                'failed local storage constructor logs and keeps region runnable',
            )
            stop()
        else:
            for key, old, new in [
                (
                    'LocalServiceModule',
                    'OpenSim.Services.UserProfilesService.dll:UserProfilesService',
                    'MissingProfile.dll:Missing',
                ),
                ('StorageProvider', 'OpenSim.Data.SQLite.dll', 'MissingStorage.dll'),
            ]:
                config.write_text(
                    settings.replace(key + '=' + old, key + '=' + new), encoding='utf-8'
                )
                start()
                check(
                    rpc('avatarclassifiedsrequest', dict(creatorId=user))['error']['code']
                    == -32604,
                    'server connector retains null service handlers after ' + key + ' failure',
                )
                stop()
    finally:
        if connection:
            connection.close()
        if process and process.poll() is None:
            process.kill()
            process.wait()
report = dict(
    passed=True,
    localRegion=a.local_region,
    controllerRequests=bool(a.requests_probe),
    checks=checks,
    applicationParityVerified=False,
    scope='Production opensim-native enabled profile connector, SQLite provider/migrations and local AccountStore identity adapter over real HTTP. Eighteen methods including preserved original error/no-op paths, durable restart/read/delete, email fallback, profile/image/notes/pick/classified updates, disabled connector and graceful stop. Local-region runs also verify module selection, empty/failed service and storage load handling; server runs retain registered null-service handlers on load failure. Viewer/cache/remote providers/global plugin loader/application/macOS incomplete.',
)
(a.work / 'results.json').write_text(json.dumps(report, indent=2))
print('PASS:', len(checks), 'production SQLite profile HTTP/persistence checks')

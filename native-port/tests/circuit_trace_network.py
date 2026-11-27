"""Check disabled, live and exit-buffered circuit diagnostics in a real process."""
import argparse
import os
import re
import subprocess
import threading


def run(binary, mode):
    environment = dict(os.environ)
    environment['OPENSIM_TRACE_CIRCUITS'] = mode
    with subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, env=environment) as process:
        lines = []
        received = threading.Event()

        def read():
            for line in process.stderr:
                lines.append(line.rstrip('\n'))
                received.set()

        reader = threading.Thread(target=read, daemon=True)
        reader.start()
        try:
            assert process.stdout.readline().strip() == 'READY'
            arrived = received.wait(.1)
            assert arrived == (mode == '1'), (mode, 'diagnostics printed before process exit')
            process.stdin.write('exit\n')
            process.stdin.flush()
            assert process.wait(timeout=10) == 0
            reader.join(timeout=5)
            assert not reader.is_alive()
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        records = [line for line in lines if line.startswith('[NATIVE CIRCUIT] probe ')]
        expected = list(range(4, 4100)) if mode == 'buffered' else list(range(4100)) if mode == '1' else []
        assert [int(line.rsplit('sequence=', 1)[1]) for line in records] == expected, mode
        if records:
            elapsed = [int(re.search(r' elapsed_us=(\d+) ', line).group(1)) for line in records]
            assert elapsed == sorted(elapsed), (mode, 'non-monotonic event timing')
            assert elapsed[-1] - elapsed[0] >= 30000, (mode, 'event timing omitted probe delays')
        if mode == 'buffered':
            assert '[NATIVE CIRCUIT BUFFER] retained=4096 dropped=4' in lines
        elif mode != '1':
            assert not lines


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    args = parser.parse_args()
    for mode in ('', '1', 'buffered'):
        run(args.native, mode)
    print('PASS: disabled/live/buffered trace modes, monotonic event timing, exit-only output and bounded oldest-record eviction')

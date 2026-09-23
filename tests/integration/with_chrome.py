#!/usr/bin/env python3
"""Run native integration programs against an owned, temporary Chrome profile."""
import argparse
import json
import functools
import http.server
import threading
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from fixture_server import FixtureHandler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--chrome', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
    parser.add_argument('--evidence', required=True)
    parser.add_argument('--binary')
    parser.add_argument('programs', nargs='+')
    args = parser.parse_args()
    evidence = Path(args.evidence).resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    results = []
    handler = functools.partial(FixtureHandler, directory=str(Path(__file__).resolve().parents[1] / 'fixtures'))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    fixture_url = 'http://127.0.0.1:' + str(server.server_port)
    profile = None
    browser = None
    try:
        with tempfile.TemporaryDirectory(prefix='chromerelay-profile-') as directory:
            profile = Path(directory)
            with (evidence / 'chrome-stderr.log').open('wb') as log:
                browser = subprocess.Popen([args.chrome, '--headless=new', '--remote-debugging-port=0',
                    '--user-data-dir=' + str(profile), '--no-first-run', '--no-default-browser-check',
                    '--disable-background-networking', '--disable-component-update', '--disable-sync',
                    '--metrics-recording-only', '--password-store=basic', '--use-mock-keychain', 'about:blank'],
                    stdout=log, stderr=log, start_new_session=True)
                try:
                    deadline = time.monotonic() + 20
                    marker = profile / 'DevToolsActivePort'
                    while not marker.exists():
                        if browser.poll() is not None:
                            raise RuntimeError('owned Chrome exited before DevTools became ready')
                        if time.monotonic() > deadline:
                            raise TimeoutError('owned Chrome did not expose DevTools within 20 seconds')
                        time.sleep(0.05)
                    port = int(marker.read_text().splitlines()[0])
                    for index, program in enumerate(args.programs):
                        command = [str(Path(program).resolve()), str(port)]
                        completed = subprocess.run(command, capture_output=True, timeout=60, env={**os.environ, "CHROMERELAY_FIXTURE_URL": fixture_url, "CHROMERELAY_CHILD_EVIDENCE": str(evidence / (Path(program).name + '-children')), **({"CHROMERELAY_BINARY": str(Path(args.binary).resolve())} if args.binary else {})})
                        (evidence / (Path(program).name + '.stdout')).write_bytes(completed.stdout)
                        (evidence / (Path(program).name + '.stderr')).write_bytes(completed.stderr)
                        results.append({'program': command[0], 'exit_code': completed.returncode,
                                        'browser_alive_after': browser.poll() is None})
                        if completed.returncode:
                            raise RuntimeError(completed.stderr.decode(errors='replace'))
                finally:
                    if browser.poll() is None:
                        os.killpg(browser.pid, signal.SIGTERM)
                        try:
                            browser.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            os.killpg(browser.pid, signal.SIGKILL)
                            browser.wait(timeout=5)
    finally:
        server.shutdown()
        server.server_close()
        serving.join(timeout=5)
        (evidence / 'owned-browser-run.json').write_text(json.dumps({
            'programs': results, 'owned_browser_pid': browser.pid if browser else None,
            'owned_browser_exit': browser.returncode if browser else None,
            'temporary_profile_removed_after_exit': profile is not None and not profile.exists(),
            'http_server_thread_stopped': not serving.is_alive()}, indent=2) + '\n')
    print(json.dumps({'passed': True, 'programs': len(results), 'profile_cleaned': not profile.exists()}))


if __name__ == '__main__':
    main()

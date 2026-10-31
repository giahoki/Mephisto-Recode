"""Exercise the bundled native binaries, rather than trusting model labels."""
import pathlib
import re
import subprocess
import threading

ROOT=pathlib.Path(__file__).resolve().parents[1]
for version in ('17','16'):
    path=ROOT/'bin/engines'/f'stockfish-{version}'/'stockfish.exe'
    process=subprocess.Popen([str(path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    watchdog=threading.Timer(15,process.kill);watchdog.start()
    try:
        process.stdin.write('uci\n');process.stdin.flush()
        identification=''
        for line in process.stdout:
            if line.startswith('id name '):identification=line.strip()
            if line.strip()=='uciok':break
        assert f'Stockfish {version}' in identification, identification
        process.stdin.write('isready\n');process.stdin.flush()
        for line in process.stdout:
            if line.strip()=='readyok':break
        process.stdin.write('position startpos\ngo movetime 100\n');process.stdin.flush()
        best=''
        for line in process.stdout:
            if line.startswith('bestmove '):best=line.split()[1];break
        assert re.fullmatch('[a-h][1-8][a-h][1-8][qrbn]?',best),best
        process.stdin.write('quit\n');process.stdin.flush();process.wait(timeout=5)
        assert process.returncode==0
        print(identification+' verified; bestmove '+best)
    finally:
        watchdog.cancel()
        if process.poll() is None:process.kill();process.wait()

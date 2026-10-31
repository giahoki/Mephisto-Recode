"""Integration tests use a separate hidden desktop process; they never arm mouse input."""
import ctypes
import http.client
import json
import pathlib
import subprocess
import socket
import shutil
import tempfile
import os
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / os.environ.get('MEPHISTO_BUILD_DIR','desktop')
PORT=17865
START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'

def post(data, origin='chrome-extension://test'):
    conn = http.client.HTTPConnection('127.0.0.1', PORT, timeout=3)
    conn.request('POST', '/state', json.dumps(data), {'Origin': origin, 'Content-Type': 'application/json'})
    response = conn.getresponse()
    status, body = response.status, response.read()
    conn.close()
    return status, body

def text_from_window():
    conn=http.client.HTTPConnection('127.0.0.1',PORT,timeout=3)
    conn.request('GET','/status',headers={'Origin':'moz-extension://integration-test'})
    response=conn.getresponse()
    text=json.loads(response.read())['text']
    conn.close()
    return text

class DesktopTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with socket.socket() as probe:
            if probe.connect_ex(('127.0.0.1',PORT)) == 0:
                raise RuntimeError('Close Mephisto before running integration tests; refusing to touch a live instance')
        cls.directory = tempfile.TemporaryDirectory(prefix='mephisto-integration-')
        cls.addClassCleanup(cls.directory.cleanup)
        shutil.copytree(BUILD,cls.directory.name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('browser-core','*.pdb','*.obj','*.cmd','*.xpi','mephisto-settings.json'))
        # Hidden window; integration harness doesn't manipulate the user's browser or cursor.
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        environment=os.environ.copy();environment['MEPHISTO_PORT']=str(PORT)
        cls.legit=os.environ.get('MEPHISTO_TEST_LEGIT')=='1'
        pathlib.Path(cls.directory.name,'mephisto-settings.json').write_text(json.dumps({'legit':cls.legit}),encoding='utf-8')
        cls.process = subprocess.Popen([str(pathlib.Path(cls.directory.name) / 'Mephisto.exe')], startupinfo=startup,env=environment)
        for _ in range(80):
            try:
                post({})
                return
            except OSError:
                time.sleep(.1)
        raise RuntimeError('Desktop server did not start')

    @classmethod
    def tearDownClass(cls):
        user = ctypes.windll.user32
        user.FindWindowW.restype = ctypes.c_void_p
        user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
        hwnd = user.FindWindowW(None, 'Mephisto Desktop - Test')
        if hwnd:
            user.PostMessageW(hwnd, 0x10, 0, 0)
        try:
            cls.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            cls.process.terminate()
            cls.process.wait()

    def state(self, **overrides):
        data = dict(session='integration', sequence=1, fen=START, placement=START.split()[0],
                    orientation='white', title='test', url='https://lichess.org/analysis',
                    left=100, top=100, width=640, height=640, focused=True, reliable=True, promotion=[])
        data.update(overrides)
        return data

    def test_01_reject_web_origin(self):
        self.assertEqual(post(self.state(), 'https://example.com')[0], 403)

    def test_02_reject_malformed_and_injected_state(self):
        self.assertEqual(post({})[0], 400)
        self.assertEqual(post(self.state(fen=START+'\ngo infinite'))[0], 400)

    def test_03_analyze_and_reject_reordering(self):
        self.assertEqual(post(self.state())[0], 200)
        self.assertEqual(post(self.state())[0], 409)
        for seq in range(2, 60):
            self.assertEqual(post(self.state(sequence=seq))[0], 200)
            text = text_from_window()
            if ('Selected move:' if self.legit else 'Best move:') in text:
                self.assertIn('Position verified', text)
                self.assertIn('Autoplay off', text)
                if self.legit:
                    self.assertIn('2. depth',text)
                    self.assertIn('3. depth',text)
                break
            time.sleep(.1)
        else:
            self.fail('No engine result: ' + text_from_window())
        time.sleep(1.6)
        self.assertIn('No fresh browser data', text_from_window())

    def test_04_new_position_cancels_old_analysis(self):
        self.assertEqual(post(self.state(session='canceled'))[0], 200)
        mate='7k/6Q1/6K1/8/8/8/8/8 b - - 0 1'
        for sequence in range(1,50):
            self.assertEqual(post(self.state(session='terminal',sequence=sequence,fen=mate,placement=mate.split()[0]))[0],200)
            text=text_from_window()
            if ('Selected move: (none)' if self.legit else 'Best move: (none)') in text:
                self.assertIn(mate,text)
                return
            time.sleep(.1)
        self.fail('Old analysis was not replaced: '+text_from_window())

    def test_05_f10_toggles_both_directions(self):
        user=ctypes.windll.user32
        user.FindWindowW.restype=ctypes.c_void_p
        user.PostMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t]
        hwnd=user.FindWindowW(None,'Mephisto Desktop - Test')
        self.assertTrue(hwnd)
        # Sends only our own window's hotkey message. No OS keyboard/mouse input.
        for expected in (True,False):
            user.PostMessageW(hwnd,0x312,3,0)
            for _ in range(20):
                text=text_from_window()
                if ('AUTOPLAY ARMED' in text)==expected:break
                time.sleep(.05)
            self.assertEqual('AUTOPLAY ARMED' in text,expected)

if __name__ == '__main__':
    unittest.main()

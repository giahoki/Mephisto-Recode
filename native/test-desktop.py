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
import zipfile

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
        if (BUILD/'Mephisto.exe').is_file():
            shutil.copytree(BUILD,cls.directory.name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('browser-core','*.pdb','*.obj','*.cmd','*.xpi','mephisto-settings.json'))
        else:
            browser='firefox' if 'firefox' in str(BUILD).lower() else 'chrome'
            with zipfile.ZipFile(ROOT/'build'/browser/'Mephisto-Program.zip') as archive:
                archive.extractall(cls.directory.name)
        # Hidden window; integration harness doesn't manipulate the user's browser or cursor.
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        environment=os.environ.copy();environment['MEPHISTO_PORT']=str(PORT)
        cls.legit=os.environ.get('MEPHISTO_TEST_LEGIT')=='1'
        cls.browser=os.environ.get('MEPHISTO_TEST_BROWSER')=='1'
        pathlib.Path(cls.directory.name,'mephisto-settings.json').write_text(json.dumps({'legit':cls.legit,'browserClicks':cls.browser}),encoding='utf-8')
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
        self.assertEqual(post(self.state(engineStartFen=START+'\nquit'))[0],400)
        self.assertEqual(post(self.state(historyLan=['e2e4\nquit']))[0],400)

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
                    self.assertRegex(text,r'3\. (depth|Engine strength-selected move)')
                    self.assertIn('Legit skill: 4 / 20',text)
                break
            time.sleep(.1)
        else:
            self.fail('No engine result: ' + text_from_window())
        time.sleep(5.3 if self.browser else 1.6)
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

    def test_06_browser_command_without_foreground(self):
        if not self.browser:self.skipTest('Only for the browser-input profile')
        user=ctypes.windll.user32;user.FindWindowW.restype=ctypes.c_void_p
        user.PostMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t]
        hwnd=user.FindWindowW(None,'Mephisto Desktop - Test')
        self.assertTrue(hwnd)
        user.PostMessageW(hwnd,0x312,3,0)
        try:
            command=None
            for seq in range(1,90):
                code,payload=post(self.state(session='background-command',sequence=seq,focused=False,clockSeconds=9,legalCount=20))
                self.assertEqual(code,200)
                result=json.loads(payload);self.assertEqual(result['inputMode'],'browser')
                if result.get('browserCommand'):
                    command=result['browserCommand'];break
                time.sleep(.1)
            self.assertIsNotNone(command,'No background command; '+text_from_window())
            self.assertRegex(command['move'],r'^[a-h][1-8][a-h][1-8][qrbn]?$')
            self.assertEqual(command['session'],'background-command')
            # Acknowledge delivery only; do not execute any input in a real browser.
            code,payload=post(self.state(session='background-command',sequence=seq+1,focused=False,
                commandResult={'id':command['id'],'status':'sent'}))
            self.assertEqual(code,200);self.assertIsNone(json.loads(payload)['browserCommand'])
        finally:user.PostMessageW(hwnd,0x312,3,0)

    def test_07_repeated_game_waits_for_new_game(self):
        for seq in range(1,30):
            self.assertEqual(post(self.state(session='threefold-game',sequence=seq,
                fen=START.replace('0 1','8 5'),threefold=True,engineStartFen=START,
                historyLan=['g1f3','g8f6','f3g1','f6g8']*2))[0],200)
            if 'трижды' in text_from_window():return
            time.sleep(.05)
        self.fail('Repeated game did not enter the waiting state')

if __name__ == '__main__':
    unittest.main()

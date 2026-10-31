"""Validate the delivered XPI and the copy inside the complete distribution."""
import io
import json
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
xpi = ROOT / 'desktop-firefox/Mephisto-Firefox-addon.xpi'

def validate(payload):
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        assert archive.testzip() is None
        names = archive.namelist()
        assert all('\\' not in name for name in names)
        manifest = json.loads(archive.read('manifest.json'))
        assert manifest['version'] == '3.0.3'
        assert 'default_locale' not in manifest
        assert '__MSG_' not in json.dumps(manifest)
        assert not any(name.startswith('_locales/') for name in names)
        assert 'service_worker' not in manifest['background']
        required = [manifest['background']['page'], manifest['action']['default_popup'],
                    'src/scripts/desktop-background.js', 'lib/chess.js']
        required += list(manifest['icons'].values())
        for script in manifest['content_scripts']:
            required += script['js']
        for name in required:
            assert archive.read(name), name
        assert b'browser.runtime' in archive.read('src/scripts/desktop-background.js')

validate(xpi.read_bytes())
delivered=(ROOT/'build/firefox/Mephisto-Firefox-addon.xpi').read_bytes()
assert delivered==xpi.read_bytes()
validate(delivered)
with zipfile.ZipFile(ROOT/'build/firefox/Mephisto-Program.zip') as program:
    assert program.testzip() is None
    assert 'Mephisto.exe' in program.namelist()
    assert not any(n.startswith('browser-core/') or n.endswith('.xpi') for n in program.namelist())
print('Firefox delivery verified: independent program ZIP + valid XPI, all references, no locale dependency.')

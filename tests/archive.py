"""Check fixed-range extraction and rejection without any vendor executable."""
from pathlib import Path
import hashlib,tempfile
from common import OUT
from package import extract_stored_setup

def archive_tests():
    directory=Path(tempfile.mkdtemp(prefix='archive-',dir=OUT/'tests'))
    payload=b'MZ_SYNTHETIC_SETUP';source=directory/'synthetic.rar'
    source.write_bytes(b'\xff\x81\x00legacy-header'+payload+b'trailer')
    meta=dict(setup_offset=16,setup_size=len(payload),setup_sha256=hashlib.sha256(payload).hexdigest())
    # Prefix has intentionally undecodable bytes; extraction never reads names.
    assert source.read_bytes()[16:16+len(payload)]==payload
    destination=directory/'setup.exe';extract_stored_setup(source,destination,meta)
    assert destination.read_bytes()==payload
    for label,changed in [('bad-hash',dict(setup_sha256='0'*64)),('negative-offset',dict(setup_offset=-1)),('truncated-range',dict(setup_size=9999))]:
        try:extract_stored_setup(source,directory/(label+'.exe'),dict(meta,**changed))
        except ValueError:pass
        else:raise AssertionError(label+' must fail closed')
    return 4

"""Validate the actual distributable, including executable permissions and native execution."""
import json
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory(prefix='vibes installed ') as directory:
    root = Path(directory)
    with zipfile.ZipFile(sys.argv[1]) as archive:
        info = archive.getinfo('extension/bin/mylang')
        mode = info.external_attr >> 16
        assert mode & 0o111, 'Packaged compiler lost executable permissions'
        assert not any('/node_modules/' in n for n in archive.namelist())
        archive.extractall(root)
    binary = root / 'extension/bin/mylang'
    binary.chmod(mode & 0o777)
    source = root / 'hello.vibes'
    source.write_text('print "Installed compiler works"\n')
    result = subprocess.run([str(binary), 'run', str(source)], cwd=root, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == 'Installed compiler works', result.stdout
    manifest = json.loads((root / 'extension/package.json').read_text())
    assert any(c['command'] == 'vibes.createProject' for c in manifest['contributes']['commands'])
    print('VSIX compiler runs outside the repository; executable permissions preserved.')

#!/usr/bin/env python3
"""Package the built NES payloads and their corresponding source.

    python3 nes/package.py        (after nes/build.sh)

Writes out/nes-release/ (of ports/): RNSE.BIN and NESCORE.BIN from the
kit's out/nes/, this port's documentation as README.md, nofrendo's
licences, nes-source.tar.gz (the corresponding source: this port and the
parts of the kit it is built from) and SHA256SUMS.  The adapter board's
release is hardware/nes-can/package.py's (rnse-hardware).  The kit is
RNSE_SDK, or rnse-sdk beside this repository.
"""
from pathlib import Path
import hashlib
import os
import shutil
import tarfile

HERE = Path(__file__).resolve().parent          # ports/nes
PORTS = HERE.parent
SDK = Path(os.environ.get('RNSE_SDK', PORTS.parent / 'sdk')).resolve()
OUT = PORTS / 'out/nes-release'
OUT.mkdir(parents=True, exist_ok=True)
for name in ('RNSE.BIN', 'NESCORE.BIN'):
    shutil.copy2(SDK / 'out/nes' / name, OUT / name)
shutil.copy2(PORTS / 'docs/nes.md', OUT / 'README.md')
for name in ('COPYING', 'COPYING.LIB', 'UPSTREAM.md'):
    shutil.copy2(HERE / 'nofrendo' / name, OUT / name)


def include(path):
    return path.is_file() and not set(path.parts) & {'__pycache__', 'build', 'out'}


# (base directory, paths under it, name in the archive)
sources = [
    (SDK, ('src', 'include', 'third_party/libc', 'link.ld', 'build.sh', 'tools/sh4-docker'), 'sdk'),
    (PORTS, ('nes', 'docs/nes.md', 'LICENSE'), 'ports'),
]
with tarfile.open(OUT / 'nes-source.tar.gz', 'w:gz') as archive:
    for base, names, arc in sources:
        for name in names:
            path = base / name
            for item in sorted(path.rglob('*')) if path.is_dir() else [path]:
                if include(item):
                    archive.add(item, arcname=f'{arc}/{item.relative_to(base)}', recursive=False)

files = sorted(path for path in OUT.iterdir() if path.is_file() and path.name != 'SHA256SUMS')
(OUT / 'SHA256SUMS').write_text(''.join(
    hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n'
    for path in files))
print(OUT)

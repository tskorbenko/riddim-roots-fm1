#!/usr/bin/env python3
"""Assemble the browser demo in demo/: the MIT fm1-emu web player (pinned
research/fm1-emu checkout), its WebAssembly machine, and the freshly built
RIDDIM ROOTS .fwsc as the only catalog entry. The page runs the real firmware
on an emulated FM-1 CPU; nothing is re-implemented in JavaScript."""
import hashlib, json, os, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, 'research', 'fm1-emu', 'web')
OUT = os.path.join(ROOT, 'demo', 'emulator')
FW = os.path.join(ROOT, 'dist', 'RIDDIM_ROOTS_v0.3.0.fwsc')
FILES = ['index.html', 'main.js', 'jit.js', 'worklet.js', 'fwsc.js', 'persistence.js', 'style.css']


def main():
    if not os.path.exists(FW):
        sys.exit('build the firmware first (tools/Build.ps1)')
    os.makedirs(OUT, exist_ok=True)
    for name in FILES:
        shutil.copyfile(os.path.join(WEB, name), os.path.join(OUT, name))
    shutil.copyfile(os.path.join(ROOT, 'research', 'fm1-emu', 'LICENSE'), os.path.join(OUT, 'LICENSE-fm1-emu.txt'))
    shutil.copyfile(os.path.join(ROOT, '.tools', 'fm1_web.wasm'), os.path.join(OUT, 'fm1_web.wasm'))
    shutil.copyfile(FW, os.path.join(OUT, 'RIDDIM_ROOTS_v0.3.0.fwsc'))
    digest = hashlib.sha256(open(FW, 'rb').read()).hexdigest()
    catalog = {
        'note': 'RIDDIM ROOTS demo catalog: one locally built firmware.',
        'default': 'riddim',
        'firmwares': [{
            'id': 'riddim', 'name': 'RIDDIM ROOTS', 'version': '0.3.0 DUB EVOLUTION',
            'author': 'RIDDIM ROOTS project', 'license': 'GPL-3.0-only WITH GPL-3.0-linking-exception (+ FM1 SDK terms)',
            'about': 'Autonomous reggae/dub band with live dub-producer controls: 11 styles, evolution, freeze, '
                     'conductor, sound-system FX and the animated Lion of Judah. Press Start, then Space (PLAY).',
            'repo': 'https://github.com/m-vave/fm1_open',
            'package': 'RIDDIM_ROOTS_v0.3.0.fwsc', 'sha256': digest,
        }],
    }
    with open(os.path.join(OUT, 'firmwares.json'), 'w', encoding='utf-8') as f:
        json.dump(catalog, f, indent=1)
    shutil.copyfile(os.path.join(ROOT, 'assets', 'lion', 'lion_source.webp'), os.path.join(ROOT, 'demo', 'lion.webp'))
    print('demo ready:', os.path.relpath(OUT, ROOT), digest)


if __name__ == '__main__':
    main()

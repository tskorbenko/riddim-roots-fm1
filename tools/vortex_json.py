#!/usr/bin/env python3
"""Generate assets/vortex/RIDDIM_ROOTS_vortex_mapping.json from the compiled-in
default mapping table in src/core/rr_map.c, so documentation and firmware agree."""
import json, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, 'src', 'core', 'rr_map.c'), encoding='utf-8').read()
names = re.search(r'rr_target_names\[RR_TARGETS\] = \{(.*?)\};', src, re.S).group(1)
names = re.findall(r'"([^"]*)"', names)
hdr = open(os.path.join(ROOT, 'src', 'core', 'rr_core.h'), encoding='utf-8').read()
targets = re.search(r'enum rr_target \{(.*?)\};', hdr, re.S).group(1)
targets = [t.strip() for t in targets.replace('\n', ' ').split(',') if t.strip()]
index = {t: i for i, t in enumerate(targets)}
table = re.search(r'static const struct rr_map defaults\[\] = \{(.*?)\n\};', src, re.S).group(1)
kinds = {'MAP_CC': 'CC', 'MAP_NOTE': 'Note', 'MAP_PC': 'ProgramChange', 'MAP_BEND': 'PitchBend', 'MAP_AT': 'Aftertouch'}
rows = []
for m in re.finditer(r'\{(MAP_\w+), (\d+), (\d+), (\w+), (T_\w+), (\d+), (\d+), (\w+)\}', table):
    k, ch, num, bank, tgt, lo, hi, fl = m.groups()
    rows.append({'type': kinds[k], 'midi_channel': int(ch) + 1, 'number': int(num),
                 'bank': 'all' if bank == 'ANY' else int(bank) + 1, 'function': names[index[tgt]],
                 'momentary': 'MOMENTARY' in fl, 'toggle': 'TOGGLE' in fl})
out = {'note': 'RIDDIM ROOTS project-defined Alesis Vortex Wireless 2 assignments. Reference only: not an Alesis .vw2 preset file.',
       'firmware': 'RIDDIM ROOTS 0.3.0', 'bank_select_program_change': {'110': 1, '111': 2, '112': 3}, 'assignments': rows}
os.makedirs(os.path.join(ROOT, 'assets', 'vortex'), exist_ok=True)
with open(os.path.join(ROOT, 'assets', 'vortex', 'RIDDIM_ROOTS_vortex_mapping.json'), 'w', encoding='utf-8') as f:
    json.dump(out, f, indent=1, ensure_ascii=False)
print(len(rows), 'assignments')

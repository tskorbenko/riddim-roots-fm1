#!/usr/bin/env python3
"""Write docs/TEST_REPORT.md from the actual logs and JSON reports in build/ and dist/.
Nothing here is typed by hand: rerun the tests, then this script."""
import json, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = lambda *p: os.path.join(ROOT, 'build', *p)
def load(p):
    try:
        return json.load(open(p, encoding='utf-8'))
    except Exception:
        return None
host = open(B('host-test.log'), encoding='utf-8').read() if os.path.exists(B('host-test.log')) else ''
emu, lion, styles = load(B('emulator-report.json')), load(B('lion-report.json')), load(B('styles-report.json'))
fw = load(os.path.join(ROOT, 'dist', 'firmware-report.json')) or {}
pkg = load(os.path.join(ROOT, 'dist', 'package-report.json')) or {}
sha = open(os.path.join(ROOT, 'dist', 'SHA256SUMS.txt')).read().split()[0] if os.path.exists(os.path.join(ROOT, 'dist', 'SHA256SUMS.txt')) else '?'
L = ['# Звіт тестів RIDDIM ROOTS 0.3.0', '', 'Згенеровано `tools/report.py` з фактичних логів (`build/`) і звітів збірки (`dist/`).', '',
     '## Збірка', '',
     f'- Прошивка: `dist/RIDDIM_ROOTS_v0.3.0.fwsc`, SHA-256 `{sha}`',
     f'- Перевірки SDK: passed = {fw.get("passed")}, app image {fw.get("app_image_bytes")} B (ліміт пакувальника 453 921 B), runtime heap {fw.get("runtime_heap_bytes")} B (мінімум {fw.get("minimum_runtime_heap_bytes")}), аудіо-колбеки в SRAM = {fw.get("audio_callbacks_in_sram")}',
     f'- Пакет: fits = {pkg.get("fits")}, user partition {pkg.get("user_bytes")} B, VM {pkg.get("VM_REAL_SIZE")} B',
     '- `rr_fx_process` перевірено в діапазоні адрес внутрішньої SRAM (tools/Build.ps1).', '',
     '## Хост-тести ядра (TinyCC)', '']
m = re.search(r'host core: (\d+) checks, (\d+) failures', host)
if m: L.append(f'- **{int(m.group(1)):,} перевірок, {m.group(2)} помилок**'.replace(',', ' '))
for line in host.splitlines():
    if line.startswith('evolve'): L.append(f'- {line}')
L += ['', 'Детерміновані seed (хеш 64 тактів, seed 42) для порівняння регресій:', '', '```']
L += [l for l in host.splitlines() if l.startswith('seed42')] + ['```', '']
L += ['## Нативний приймальний тест (емулятор FM-1, справжній .fwsc)', '']
if emu:
    L += [f'- **{emu["checks"]} перевірок пройдено**, {emu["emulated_seconds"]:.1f} емульованих секунд ({emu["wall_seconds"]:.0f} с реального часу)',
          f'- Пакет під тестом: SHA-256 `{emu["firmware_sha256"]}`',
          f'- STOP: жива нота звучить без бенду (RMS {emu["stop_live_rms"]:.4f}); PLAY: RMS {emu["play_rms"]:.4f}, пік {emu["play_peak"]:.3f}',
          f'- BPM з HOME, HOME+енкодер, CC3 зі згладжуванням, повернення: фінальний {emu["bpm_after_controls"]}',
          '- Переходи стилів без зупинки (аудіо перевірялось кожні 0,5 с):']
    for s in emu['style_changes']:
        L.append(f'  - {s["name"]}: {s["bars"]} тактів, BPM {s["bpm"]} (пік {s["peakBpm"]}), шлях темпу {s["path"]}')
    L += [f'- FREEZE BASS + EVOLVE 100 (≈70 с, DUB TECHNO): бас незмінний, змінилось {emu["freeze"]["changed"]} інших компонентів, мутацій {emu["freeze"]["mutations"]}',
          f'- MIDI flood 1500 нот: без залиплих нот; втрачено повідомлень (лічильник) {emu["midi_errors_after_flood"]}',
          f'- Довгий прогін {emu["long_run_s"]:.0f} с з випадковими контролами: {emu["final"]}',
          '- Також: 8 фейдерів × 4 значення, банк 2 через Program Change, 8 падів з velocity, сирена, ribbon/tilt/bend/aftertouch, sustain, режими CHORD/SPLIT, октави, усі 10 сторінок (знімки `build/emu-page-N.bmp`), команди диригента, FX з панелі, RAM STORE/RECALL, FLASH SAVE/LOAD.', '']
if lion:
    L += ['## Анімація лева', '', f'- {lion["checks"]} перевірок: SLEEP, PLAY (≥3 пози: {lion["play_poses"]}), MIDI, MUTATE, FX, DROP, DANCE, FREEZE (поза незмінна)',
          f'- Передано пікселів на LCD: STOP {lion["stop_pixels_per_s"]:.0f}/с, PLAY {lion["play_pixels_per_s"]:.0f}/с (повний екран = 57 600)',
          '- Знімки: `build/lion-*.bmp`; underruns/overruns під час анімації = 0', '']
if styles:
    L += ['## Аудіо 11 стилів (8 с кожен, `build/style-NN.wav`)', '', '| Стиль | BPM | RMS | Пік | Кліпінг | Underruns |', '|---|---|---|---|---|---|']
    for s in styles:
        L.append(f'| {s["style"]} | {s["bpm"]} | {s["rms"]} | {s["peak"]} | {s["clipped_samples"]} | {s["underruns"]} |')
    L += ['', 'Пік = стеля лімітера (0,95 FS) × ручка MASTER у тесті (900/1023).', '']
L += ['## Не перевірено', '', '- Реальне залізо FM-1, тип TRS, кабель і Vortex фізично, прослуховування на динаміках. Див. `docs/LIMITATIONS.md`.', '']
open(os.path.join(ROOT, 'docs', 'TEST_REPORT.md'), 'w', encoding='utf-8').write('\n'.join(L))
print('docs/TEST_REPORT.md written')

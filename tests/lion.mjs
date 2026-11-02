// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Native lion animation acceptance: modes from firmware RAM, pixels from the LCD,
// partial-redraw cost from the UI's own submitted-pixel counter.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { FM1, BTN, ENC, D } from './fm1-runtime.mjs';
const L = { PLAY: 0, SLEEP: 1, FREEZE: 2, MUTATE: 3, FX: 4, DROP: 5, DANCE: 6, MIDI: 7 };
const e = await new FM1().boot();
const report = {}; let checks = 0;
const ok = (c, m) => { checks++; assert.ok(c, m); };
const lionBox = (lcd) => { let h = 0; for (let y = 36; y < 124; y++) for (let x = 136; x < 236; x++) h = (h * 31 + lcd[y * 240 + x]) >>> 0; return h; };
e.advance(2600); e.x.fm1_pot(900);
// STOP: sleeping lion, nearly idle display
ok(e.d('lion') === L.SLEEP, 'STOP: lion sleeps');
let p0 = e.d('pixels'); e.advance(3000); let idle = e.d('pixels') - p0;
report.stop_pixels_per_s = idle / 3;
ok(idle < 240 * 240 * 1.5, `STOP: partial redraws only, < 0.5 screen/s (${idle} px in 3 s)`);
e.snapshot('lion-sleep');
// PLAY: nod in time, partial redraws only
e.key(BTN.PLAY); e.advance(1500);
ok(e.d('lion') === L.PLAY, 'PLAY: lion awake');
const poses = new Set(); p0 = e.d('pixels');
for (let i = 0; i < 40; i++) { e.advance(70); poses.add(lionBox(e.snapshot('lion-play'))); }
const playPixels = (e.d('pixels') - p0) / 2.8;
report.play_pixels_per_s = playPixels; report.play_poses = poses.size;
ok(poses.size >= 3, `lion nods/bounces with the beat (${poses.size} poses)`);
ok(playPixels < 240 * 240 * 6, `partial redraws while playing (${Math.round(playPixels)} px/s)`);
// MIDI input reacts with velocity
e.midi(0x90, 60, 127); e.advance(80); ok(e.d('lion') === L.MIDI, 'MIDI note: lion reacts'); e.snapshot('lion-midi'); e.midi(0x80, 60, 0); e.advance(500);
// MUTATE: pad 7
e.midi(0x99, 42, 100); e.advance(100); ok(e.d('lion') === L.MUTATE, 'MUTATE: head shake'); e.snapshot('lion-mutate'); e.midi(0x89, 42, 0); e.advance(900);
// DUB FX: hold FX button + key
e.x.fm1_key(BTN.FX, 1); e.advance(40); e.x.fm1_key(14, 1); e.advance(300);
ok(e.d('lion') === L.FX, 'DUB FX: blink, flag, mane'); e.snapshot('lion-fx');
e.x.fm1_key(14, 0); e.x.fm1_key(BTN.FX, 0); e.advance(1600);
// DROP via conductor key (hold ARP)
e.x.fm1_key(BTN.ARP, 1); e.advance(40); e.note(25, 60); e.x.fm1_key(BTN.ARP, 0); e.advance(150);
ok(e.d('lion') === L.DROP, 'DROP: lion leans back'); e.snapshot('lion-drop'); e.advance(1600);
// STYLE CHANGE: dance during the transition
e.cc(9, Math.ceil(3 * 128 / 11)); e.advance(200);
e.waitFor(() => e.d('lion') === L.DANCE, 3000, 'dance'); ok(e.d('lion') === L.DANCE, 'STYLE CHANGE: dance'); e.snapshot('lion-dance');
e.waitFor(() => !e.d('transition'), 60000, 'transition');
// FREEZE: latched pose
e.x.fm1_key(BTN.LFO, 1); e.advance(40); e.note(0); e.x.fm1_key(BTN.LFO, 0); e.advance(100);
ok(e.d('lion') === L.FREEZE, 'FREEZE: lion stills');
const a = lionBox(e.snapshot('lion-freeze')); e.advance(400); const b = lionBox(e.snapshot('lion-freeze'));
ok(a === b, 'frozen pose does not move');
ok(e.d('underruns') === 0 && e.d('overruns') === 0, 'animation never disturbs audio');
report.checks = checks;
writeFileSync('build/lion-report.json', JSON.stringify(report, null, 2));
console.log(`lion animation: ${checks} checks passed`, JSON.stringify(report));

// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Native acceptance: the built .fwsc runs on the emulated FM-1 CPU. Every
// assertion reads firmware RAM, the LCD or the emulated audio output.
import assert from 'node:assert/strict';
import { writeFileSync, readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { FM1, BTN, ENC, D } from './fm1-runtime.mjs';

const started = performance.now();
const ev = { checks: 0 };
const ok = (c, msg) => { ev.checks++; assert.ok(c, msg); };
const e = await new FM1().boot();
const fw = readFileSync('dist/RIDDIM_ROOTS_v0.3.0.fwsc');
ev.firmware_sha256 = createHash('sha256').update(fw).digest('hex');
const log = (name, extra = {}) => { const d = e.diag(); console.log(name.padEnd(22), JSON.stringify({ ms: d[D.ms], style: d[D.style], bpm: d[D.bpm], bars: d[D.bar_played], under: d[D.underruns], over: d[D.overruns], ...extra })); };

// ---- boot ----
e.advance(2600); e.x.fm1_pot(900); e.advance(300);
let d = e.diag();
ok(d[D.playing] === 0 && d[D.style] === 2 && d[D.bpm] === 72, 'boots stopped in ROOTS at 72');
e.snapshot('emu-home-stop');
ok(d[D.lion] === 1, 'lion sleeps while stopped');

// ---- STOP: live keyboard plays without the band ----
e.down(14 + 7);
let rms = e.advance(600);
ok(e.d('live_notes') >= 1 && rms > 0.002, `FM-1 key sounds while stopped (rms ${rms})`);
e.up(14 + 7);
e.advance(400);
ok(e.d('live_notes') === 0, 'key released');
e.midi(0x90, 57, 100); rms = e.advance(500);
ok(e.d('live_notes') === 1 && rms > 0.002, 'MIDI note from DIN plays while stopped');
e.midi(0x80, 57, 0); e.advance(200);
ev.stop_live_rms = rms;

// ---- PLAY ----
e.key(BTN.PLAY);
ok(e.d('playing') === 1, 'PLAY starts the band');
rms = e.advance(7000, true);
d = e.diag();
ok(d[D.bar_played] >= 2 && rms > 0.01, `band audible (rms ${rms.toFixed(4)})`);
ok(d[D.lion] !== 1, 'lion awake while playing');
ev.play_rms = rms; ev.play_peak = e.peak;
e.snapshot('emu-home-play');
log('play');

// ---- held live note survives STOP and PLAY ----
e.midi(0x90, 64, 110); e.advance(300);
e.key(BTN.PLAY);
ok(e.d('playing') === 0 && e.d('live_notes') === 1, 'STOP keeps the held live note');
rms = e.advance(500);
ok(rms > 0.002, 'held note audible after STOP');
e.key(BTN.PLAY);
ok(e.d('playing') === 1 && e.d('live_notes') === 1, 'PLAY again keeps live MIDI');
e.midi(0x80, 64, 0); e.advance(200);
ok(e.d('live_notes') === 0, 'note off releases');

// ---- BPM from HOME: encoder, coarse, MIDI CC, tap ----
const bpm0 = e.d('bpm');
e.enc(ENC.BPM, 3); e.advance(1200);
ok(e.d('bpm') === bpm0 + 3 && e.d('playing') === 1, 'ALGORITHM encoder changes BPM while playing');
e.x.fm1_key(BTN.HOME, 1); e.enc(ENC.BPM, 2); e.x.fm1_key(BTN.HOME, 0); e.advance(2500);
ok(e.d('bpm') === bpm0 + 13, `HOME + encoder = coarse BPM (${e.d('bpm')})`);
e.cc(3, Math.round((90 - 50) / 150 * 127)); e.advance(3000);
ok(Math.abs(e.d('bpm') - 90) <= 1, `MIDI CC3 tempo with smoothing (${e.d('bpm')})`);
e.enc(ENC.BPM, -18); e.advance(2500);
log('bpm');
ev.bpm_after_controls = e.d('bpm');

// ---- style transitions without stopping ----
const styleChanges = [];
for (const [style, name, bpm] of [[3, 'STEPPERS', 78], [8, 'DUB TECHNO', 122], [7, 'RAGGA JUNGLE', 164], [2, 'ROOTS', 72]]) {
  e.cc(9, Math.ceil(style * 128 / 11)); e.advance(200);
  ok(e.d('target') === style, `${name} queued`);
  let minPlaying = 1, peakBpm = 0, bars = e.d('bar_played');
  const path = [];
  for (let t = 0; t < 60000 && (e.d('transition') || e.d('style') !== style || Math.abs(e.d('bpm') - bpm) > 1); t += 500) {
    path.push(e.d('bpm'));
    const r = e.advance(500); minPlaying = Math.min(minPlaying, e.d('playing')); peakBpm = Math.max(peakBpm, e.d('bpm'));
    ok(r > 0.001, `${name}: audio continues during transition`);
  }
  ok(e.d('style') === style && minPlaying === 1, `${name} reached without stopping`);
  ok(Math.abs(e.d('bpm') - bpm) <= 1, `${name} tempo ${e.d('bpm')}`);
  styleChanges.push({ name, bars: e.d('bar_played') - bars, bpm: e.d('bpm'), peakBpm, path: path.filter((v, i) => i % 2 === 0) });
  log('style ' + name);
}
ev.style_changes = styleChanges;
ok(styleChanges[2].peakBpm >= 160, 'jungle tempo reached via double-time plan');
e.snapshot('emu-home-roots-again');

// ---- faders bank 1 (mixer) with soft takeover ----
for (let f = 0; f < 8; f++) { for (const v of [0, 30, 64, 90]) e.fader(f, v); }
d = e.diag();
ok(d[D.last_target] >= 1 && d[D.cc_in] >= 32, 'faders reach mixer targets');
// bank 2 = EVOLVE via Program Change 111
e.midi(0xc1 - 1, 111); e.advance(100);
ok(e.d('bank') === 1, 'Program Change selects bank 2');
for (const v of [0, 20, 40, 60, 80, 100, 127]) e.fader(7, v);
ok(e.d('evolve') === 100, `fader 8 = global EVOLVE (${e.d('evolve')})`);

// ---- freeze bass, evolve 100: bass motif stays, others change ----
e.x.fm1_key(BTN.LFO, 1); e.advance(60); e.note(2); e.x.fm1_key(BTN.LFO, 0); e.advance(100);
ok((e.d('freeze') & 4) === 4, 'LFO + key 3 freezes BASS');
const before = e.diag().slice(D.hash, D.hash + 8);
e.cc(9, Math.ceil(8 * 128 / 11)); // DUB TECHNO: 122 BPM, shorter phrases
e.advance(70000);
const after = e.diag().slice(D.hash, D.hash + 8);
ok(after[2] === before[2], 'frozen bass motif unchanged at EVOLVE 100');
const changed = after.filter((h, i) => i !== 2 && h !== before[i]).length;
ok(changed >= 4, `unfrozen components evolved (${changed})`);
ok(e.d('style') === 8, 'style moved to DUB TECHNO with frozen bass');
ev.freeze = { changed, mutations: e.d('mutations') };
log('freeze+evolve', { changed });
e.snapshot('emu-home-evolve');

// ---- pads (velocity), ribbon, tilt, bend, aftertouch, sustain ----
for (let p = 0; p < 8; p++) { e.midi(0x99, 36 + p, 40 + p * 10); e.advance(400); e.midi(0x89, 36 + p, 0); e.advance(300); }
ok(e.d('playing') === 1, 'all pads triggered while playing');
e.midi(0x99, 36, 120); e.advance(200); ok(e.d('fx_voices') >= 1, 'siren voice sounding'); e.midi(0x89, 36, 0); e.advance(1500);
for (const v of [10, 60, 120, 64]) { e.cc(74, v); e.cc(71, v); e.cc(16, v); e.cc(17, v); }
for (const v of [0, 4000, 8192, 16000, 8192]) e.midi(0xe0, v & 127, v >> 7);
e.midi(0xd0, 90); e.midi(0xd0, 0);
e.midi(0xb0, 64, 127); e.midi(0x90, 60, 90); e.midi(0x80, 60, 0); e.advance(300);
ok(e.d('live_notes') === 1, 'sustain holds the note');
e.midi(0xb0, 64, 0); e.advance(200);
ok(e.d('live_notes') === 0, 'sustain release ends it');
e.midi(0xb0, 7, 100);
ok(e.d('playing') === 1, 'expression controls processed');

// ---- keyboard modes (CHORD, SPLIT) and octave ----
e.x.fm1_key(BTN.SCL, 1); e.advance(60); e.x.fm1_key(BTN.SCL, 0); e.advance(100);
ok(e.d('page') === 5, 'SCL opens LIVE');
e.enc(ENC.K1, 1);
ok(e.d('kmode') === 1, 'K1 selects CHORD mode');
e.midi(0x90, 57, 100); e.advance(150); ok(e.d('live_notes') === 3, 'chord mode plays a triad'); e.midi(0x80, 57, 0); e.advance(150);
e.enc(ENC.K1, 4); ok(e.d('kmode') === 5, 'SPLIT mode');
e.midi(0x90, 40, 100); e.midi(0x90, 72, 100); e.advance(150); ok(e.d('live_notes') === 2, 'split plays bass + lead'); e.midi(0x80, 40, 0); e.midi(0x80, 72, 0);
e.enc(ENC.K1, -5); e.key(BTN.OCTUP); e.note(0); e.key(BTN.OCTDN);
e.snapshot('emu-live');

// ---- all pages ----
e.key(BTN.HOME);
for (let p = 0; p < 10; p++) {
  e.advance(250);
  ok(e.d('page') === p, `page ${p}`);
  e.snapshot(`emu-page-${p}`);
  e.enc(ENC.SELECT, 1);
}

// ---- conductor commands and FX from the panel ----
e.x.fm1_key(BTN.ARP, 1); e.advance(50);
for (const k of [0, 4, 23, 27]) e.note(k, 60);
e.x.fm1_key(BTN.ARP, 0); e.advance(2000);
e.x.fm1_key(BTN.FX, 1); e.advance(50);
for (const k of [0, 1, 2, 3, 4, 5]) e.note(k, 200);
e.x.fm1_key(BTN.FX, 0); e.advance(1500);
ok(e.d('playing') === 1 && e.d('live_notes') === 0, 'commands/FX left no stuck notes');

// ---- MIDI flood ----
for (let i = 0; i < 1500; i++) { e.x.fm1_midi(0x90); e.x.fm1_midi(48 + (i % 24)); e.x.fm1_midi(i & 1 ? 0 : 90); }
e.advance(1500);
e.midi(0xb0, 123, 0); e.advance(300);
ok(e.d('live_notes') === 0 && e.d('playing') === 1, 'MIDI flood survived without stuck notes');
ev.midi_errors_after_flood = e.d('midi_errors');

// ---- memory: store/recall in RAM, save/load in flash ----
e.x.fm1_key(BTN.SAVE, 1); e.advance(60); e.x.fm1_key(BTN.SAVE, 0); e.advance(100);
ok(e.d('page') === 9, 'SAVE opens SYSTEM');
e.key(BTN.SAVE); e.advance(200);                      // action STORE MEM, slot M1
ok((e.d('mem_used') & 1) === 1, 'stored in M1');
e.enc(ENC.K2, 2); e.key(BTN.SAVE); e.advance(2000);    // SAVE FLASH
ok(e.d('save_result') === 0, `flash save (${e.d('save_result')})`);
e.enc(ENC.K2, 1); e.key(BTN.SAVE); e.advance(3000);    // LOAD FLASH
ok(e.d('load_result') === 0, `flash load (${e.d('load_result')})`);
e.waitFor(() => e.d('playing') === 1, 10000, 'playback after load');
e.enc(ENC.K2, -2); e.key(BTN.SAVE); e.advance(500);    // RECALL M1
log('memory');

// ---- long run at high evolution with random controls ----
const t0 = e.d('ms');
let silent = 0;
for (let s = 0; s < 60; s++) {
  const r = e.advance(1000);
  if (r < 0.001) silent++;
  if (s % 7 === 0) e.cc(27, (s * 37) % 128);
  if (s % 11 === 0) e.midi(0x99, 36 + (s % 8), 100), e.midi(0x89, 36 + (s % 8), 0);
}
d = e.diag();
ok(silent <= 4, `long run stays musical (${silent} quiet seconds)`);
ok(d[D.underruns] === 0 && d[D.overruns] === 0, `no audio underruns/overruns (${d[D.underruns]}/${d[D.overruns]})`);
ev.long_run_s = (d[D.ms] - t0) / 1000;
ev.final = { underruns: d[D.underruns], overruns: d[D.overruns], bars: d[D.bar_played], mutations: d[D.mutations], peak: e.peak, shed: d[D.shed] };
e.wav('build/emulator-session.wav');
ev.emulated_seconds = e.ms() / 1000;
ev.wall_seconds = (performance.now() - started) / 1000;
writeFileSync('build/emulator-report.json', JSON.stringify(ev, null, 2));
console.log(`native acceptance: ${ev.checks} checks passed, ${ev.emulated_seconds.toFixed(1)} emulated s`);

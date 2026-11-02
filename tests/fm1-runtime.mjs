// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Runs the real RIDDIM ROOTS .fwsc on the fm1-emu WebAssembly machine (PI32v2 CPU,
// LCD, key matrix, MIDI UART, audio). Diagnostics are read from the firmware's
// own RAM block (magic "RRD3"), never from a host re-implementation.
import { readFileSync, writeFileSync } from 'node:fs';
import assert from 'node:assert/strict';
import { jitImports } from '../research/fm1-emu/web/jit.js';

export const D = Object.fromEntries(['magic', 'ms', 'playing', 'style', 'target', 'transition', 'bpm', 'evolve', 'freeze', 'bar_played',
  'section', 'live_notes', 'notes_in', 'cc_in', 'midi_errors', 'page', 'kmode', 'mutations', 'fx_blocks', 'fx_peak',
  'underruns', 'overruns', 'lion', 'bank', 'last_target', 'last_value', 'active', 'energy', 'dub', 'bpm_target']
  .map((k, i) => [k, i]));
D.hash = 30; D.inst = 38; D.level = 46; D.dim = 52; D.mem_used = 59; D.fx_voices = 60; D.version = 61;
D.save_result = 62; D.load_result = 63; D.shed = 64; D.pixels = 65; D.polls = 66;
export const BTN = { OCTDN: 0, OCTUP: 1, FX: 2, SCL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
export const ENC = { SELECT: 0, BPM: 1, K1: 2, K2: 3, K3: 4, K4: 5, STYLE: 6 };

export class FM1 {
  async boot(file = 'dist/RIDDIM_ROOTS_v0.3.0.fwsc', flash) {
    this.firmware = readFileSync(file);
    this.jit = { regions: 0, failed: 0, compileMs: 0, compileMaxMs: 0, bytes: 0 };
    let x;
    const { instance } = await WebAssembly.instantiate(readFileSync('.tools/fm1_web.wasm'), { env: jitImports(() => x, this.jit) });
    x = instance.exports; this.x = x;
    x.fm1_jit(1, 16); x.fm1_ahead(1024);
    assert.equal(x.fm1_boot_fwsc(this.copy(this.firmware), this.firmware.length, 0, 0, 240), 0, 'package boots');
    if (flash) assert.equal(x.fm1_flash_restore(this.copy(flash), flash.length), 0);
    this.diagPtr = 0; this.samples = []; this.peak = 0; this.frames = 0;
    return this;
  }
  copy(bytes) { const p = this.x.fm1_alloc(bytes.length); new Uint8Array(this.x.memory.buffer, p, bytes.length).set(bytes); return p; }
  ms() { return this.x.fm1_ms(); }
  advance(ms, record = false) {
    const x = this.x, until = x.fm1_ms() + ms; let sum = 0, count = 0;
    while (x.fm1_ms() < until) {
      const n = x.fm1_render(128, 4_800_000);
      assert.equal(x.fm1_halted(), 0, 'native CPU halted');
      this.frames += n;
      if (!n) continue;
      const l = new Float32Array(x.memory.buffer, x.fm1_out_l(), n), r = new Float32Array(x.memory.buffer, x.fm1_out_r(), n);
      for (let i = 0; i < n; i++) {
        assert.ok(Number.isFinite(l[i]) && Number.isFinite(r[i]), 'finite audio');
        const a = Math.max(Math.abs(l[i]), Math.abs(r[i])); if (a > this.peak) this.peak = a;
        sum += l[i] * l[i] + r[i] * r[i]; count += 2;
        if (record) this.samples.push(Math.max(-32768, Math.min(32767, Math.round(l[i] * 32767))), Math.max(-32768, Math.min(32767, Math.round(r[i] * 32767))));
      }
    }
    return Math.sqrt(sum / Math.max(1, count));
  }
  diag() {
    const x = this.x;
    if (!this.diagPtr) {
      const v = new Uint32Array(x.memory.buffer);
      for (let i = 0; i < v.length - 80; i++) if (v[i] === 0x33445252 && Math.abs(v[i + 1] - x.fm1_ms()) < 500 && v[i + 3] < 11) { this.diagPtr = i * 4; break; }
    }
    assert.ok(this.diagPtr, 'firmware diagnostics found');
    return Array.from(new Uint32Array(x.memory.buffer, this.diagPtr, 70));
  }
  d(name) { return this.diag()[D[name]]; }
  key(id, hold = 90) { this.x.fm1_key(id, 1); this.advance(hold); this.x.fm1_key(id, 0); this.advance(90); }
  down(id) { this.x.fm1_key(id, 1); this.advance(40); }
  up(id) { this.x.fm1_key(id, 0); this.advance(40); }
  note(k, hold = 120) { this.key(14 + k, hold); }      // FM-1 keyboard key 0..26
  enc(id, delta) { this.x.fm1_enc(id, delta); this.advance(120); }
  midi(...bytes) { for (const b of bytes) this.x.fm1_midi(b); this.advance(25); }
  cc(n, v, ch = 0) { this.midi(0xb0 | ch, n, v); }
  fader(n, v) { this.cc(20 + n, v); }
  waitFor(test, ms = 20000, what = 'state') { for (let t = 0; t < ms; t += 50) { if (test()) return; this.advance(50); } assert.ok(test(), `timed out waiting for ${what}`); }
  snapshot(name) {
    const lcd = new Uint16Array(this.x.memory.buffer, this.x.fm1_lcd(), 57600), b = Buffer.alloc(54 + 172800);
    b.write('BM'); b.writeUInt32LE(b.length, 2); b.writeUInt32LE(54, 10); b.writeUInt32LE(40, 14); b.writeInt32LE(240, 18);
    b.writeInt32LE(240, 22); b.writeUInt16LE(1, 26); b.writeUInt16LE(24, 28);
    for (let y = 0; y < 240; y++) for (let x = 0; x < 240; x++) {
      const v = lcd[y * 240 + x], p = 54 + ((239 - y) * 240 + x) * 3;
      b[p] = (v & 31) * 255 / 31; b[p + 1] = ((v >> 5) & 63) * 255 / 63; b[p + 2] = (v >> 11) * 255 / 31;
    }
    writeFileSync(`build/${name}.bmp`, b);
    return lcd.slice();
  }
  wav(path) {
    const pcm = Buffer.alloc(this.samples.length * 2); this.samples.forEach((v, i) => pcm.writeInt16LE(v, i * 2));
    const h = Buffer.alloc(44); h.write('RIFF'); h.writeUInt32LE(pcm.length + 36, 4); h.write('WAVEfmt ', 8); h.writeUInt32LE(16, 16);
    h.writeUInt16LE(1, 20); h.writeUInt16LE(2, 22); h.writeUInt32LE(44100, 24); h.writeUInt32LE(176400, 28); h.writeUInt16LE(4, 32);
    h.writeUInt16LE(16, 34); h.write('data', 36); h.writeUInt32LE(pcm.length, 40); writeFileSync(path, Buffer.concat([h, pcm]));
  }
}

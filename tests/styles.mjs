// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Records 8 s of every style from the native firmware (build/style-NN.wav) and
// measures level, peak and clipping, so audio regressions can be compared.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { FM1, BTN, ENC, D } from './fm1-runtime.mjs';
const names = ['SKA', 'ROCKSTEADY', 'ROOTS', 'STEPPERS', 'DUB', 'RUB-A-DUB', 'DANCEHALL', 'RAGGA JUNGLE', 'DUB TECHNO', 'DEEP DUBSTEP', 'DUB NOISE'];
const out = [];
for (let s = 0; s < 11; s++) {
  const e = await new FM1().boot();
  e.advance(2600); e.x.fm1_pot(900);
  e.enc(ENC.STYLE, s - 2); e.advance(900);
  assert.equal(e.d('style'), s, `style ${names[s]} selected while stopped`);
  e.key(BTN.PLAY); e.advance(500);
  e.samples = []; e.peak = 0;
  const rms = e.advance(8000, true);
  let clip = 0; for (const v of e.samples) if (Math.abs(v) >= 32760) clip++;
  e.wav(`build/style-${String(s).padStart(2, '0')}.wav`);
  const r = { style: names[s], bpm: e.d('bpm'), rms: +rms.toFixed(4), peak: +e.peak.toFixed(3), clipped_samples: clip, underruns: e.d('underruns') };
  out.push(r); console.log(JSON.stringify(r));
  assert.ok(rms > 0.01, `${names[s]} audible`); assert.equal(clip, 0, `${names[s]} does not clip`); assert.equal(r.underruns, 0);
}
writeFileSync('build/styles-report.json', JSON.stringify(out, null, 2));

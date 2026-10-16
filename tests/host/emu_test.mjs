// SPDX-License-Identifier: GPL-3.0-only
// The browser build in Node: boot it, press PLAY, expect MIDI out (in time order), no clipping, screen frames; the flash
// kept and given back.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();
const PLAY = 10, SAVE = 7;
let peak = 0, notes = 0, last = -1, backwards = 0;
for (let i = 0; i < 1600; i++) {                       // ~4.6 s
  if (i === 20) ex.web_buttons(1 << PLAY); else if (i === 40) ex.web_buttons(0);
  if (i === 1400) ex.web_buttons(1 << SAVE); else if (i === 1420) ex.web_buttons(0);
  ex.web_render(128);
  const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
  for (const v of l) peak = Math.max(peak, Math.abs(v));
  const n = ex.web_midi_take(), b = new Uint32Array(mem.buffer, ex.web_midi_buf(), 2 * n);
  for (let k = 0; k < n; k++) {
    const st = (b[2 * k + 1] >>> 16) & 255;
    if ((st & 0xF0) === 0x90 && (b[2 * k + 1] & 127)) notes++;
    if (b[2 * k] < last) backwards++;
    last = b[2 * k];
  }
}
const writes = ex.web_flash_writes(), len = ex.web_flash_len();
console.log("peak", peak.toFixed(3), "notes", notes, "blits", ex.web_blits(), "flash writes", writes, "kept", len, "B");
process.exit(peak < 1 && notes > 20 && !backwards && writes > 0 && ex.web_blits() > 10 ? 0 : 1);

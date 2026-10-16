// SPDX-License-Identifier: GPL-3.0-only
// SEQ-4 in an AudioWorklet: seq4.wasm is the whole FM-1 app (web/emu/seq4_web.c). Each render quantum asks it
// for 128 frames, which runs the device's clock forward; what it played goes to the page at once, with the
// device's time of each message (the page sends it on with WebMIDI, timed). Between quanta the page's input
// goes in and, about 30 times a second, the screen, the lights and (when it changed) the flash go out.

const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();

class Seq4 extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.sentBlits = -1;
    this.sentWrites = -1;
    this.flashAt = 0;
    this.lastFrame = 0;
    this.busy = 0;
    this.frames = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      const now = () => BigInt(Math.round(currentTime * 1e9));
      const wasi = {
        clock_time_get: (id, prec, out) => {
          new DataView(this.mem.buffer).setBigUint64(out, now(), true);
          return 0;
        },
        fd_write: (fd, iov, n, out) => { new DataView(this.mem.buffer).setUint32(out, 0, true); return 0; },
        proc_exit: () => {},
      };
      const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
      const env = new Proxy({}, { get: () => () => 0 });
      const { instance } = await WebAssembly.instantiate(m.wasm, { wasi_snapshot_preview1: stub, env });
      this.ex = instance.exports;
      this.mem = this.ex.memory;
      if (this.ex._initialize) this.ex._initialize();
      if (m.flash) {                                  // the flash as this browser kept it
        const len = this.ex.web_flash_len(), b = new Uint8Array(m.flash);
        this.ex.web_flash_erased();
        if (b.length === len) new Uint8Array(this.mem.buffer, this.ex.web_flash(), len).set(b);
      }
      this.ex.web_master(m.master ?? 2800);
      this.ex.web_boot();
      this.sentWrites = this.ex.web_flash_writes();
      this.port.postMessage({ type: "ready" });
    } else if (!this.ex) {
      return;
    } else if (m.type === "buttons") {
      this.ex.web_buttons(m.mask >>> 0);
    } else if (m.type === "keys") {
      this.ex.web_keys(m.mask >>> 0);
    } else if (m.type === "enc") {
      this.ex.web_enc(m.role, m.n | 0);
    } else if (m.type === "master") {
      this.ex.web_master(m.value | 0);
    } else if (m.type === "midi") {                   // a message from a MIDI input: [status, d1, d2]
      const [st, d1 = 0, d2 = 0] = m.data;
      const cin = st >= 0xF0 ? 0x0F : st >> 4;
      this.ex.web_midi_in(((m.cable & 15) << 4 | cin | st << 8 | d1 << 16 | d2 << 24) >>> 0);
    }
  }

  midi() {
    const n = this.ex.web_midi_take();
    if (!n) return;
    const b = new Uint32Array(this.mem.buffer, this.ex.web_midi_buf(), 2 * n).slice();
    this.port.postMessage({ type: "midi", ev: b, at: currentTime }, [b.buffer]);
  }

  publish() {
    const ex = this.ex;
    const blits = ex.web_blits();
    const msg = { type: "frame", buttons: ex.web_lit_buttons() >>> 0, keys: ex.web_lit_keys() >>> 0 };
    if (this.frames >= 44100) {                       // the share of real time spent running the device
      msg.load = Math.round(100 * this.busy / (this.frames / 44.1));
      this.busy = this.frames = 0;
    }
    const transfer = [];
    if (blits !== this.sentBlits) {
      this.sentBlits = blits;
      msg.fb = new Uint16Array(this.mem.buffer, ex.web_fb(), 240 * 240).slice();
      transfer.push(msg.fb.buffer);
    }
    const writes = ex.web_flash_writes();             // the flash changed: to the page, at most every 2 s
    if (writes !== this.sentWrites && currentTime - this.flashAt > 2) {
      this.sentWrites = writes;
      this.flashAt = currentTime;
      msg.flash = new Uint8Array(this.mem.buffer, ex.web_flash(), ex.web_flash_len()).slice();
      transfer.push(msg.flash.buffer);
    }
    this.port.postMessage(msg, transfer);
  }

  process(inputs, outputs) {
    if (!this.ex) return true;
    const out = outputs[0];
    const n = out[0].length;
    const t0 = clock();
    this.ex.web_render(n);
    this.busy += clock() - t0;
    this.frames += n;
    out[0].set(new Float32Array(this.mem.buffer, this.ex.web_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(this.mem.buffer, this.ex.web_out_r(), n));
    this.midi();
    if (currentTime - this.lastFrame >= 1 / 30) {
      this.lastFrame = currentTime;
      this.publish();
    }
    return true;
  }
}

registerProcessor("seq4", Seq4);

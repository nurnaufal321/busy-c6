// The BUSY C6 serial protocol: text lines over the board's USB port. See PROTOCOL.md.
'use strict';

const ESPRESSIF_USB_VENDOR = 0x303a;
const CUSTOM_PIXELS = 72 * 16;
// The first firmware that can move the custom status by half an LED.
const HALF_LED_VERSION = '1.1.0';
// The first firmware that keeps several named custom statuses.
const SAVED_CUSTOMS_VERSION = '1.2.0';
const CUSTOM_MAX = 8;

/** -1, 0 or 1 as version [a] is older, the same or newer than [b]. Anything that isn't X.Y.Z is older. */
function compareVersions(a, b) {
  const parse = (v) => /^\d+(\.\d+)*$/.test(v ?? '') ? v.split('.').map(Number) : null;
  const pa = parse(a), pb = parse(b);
  if (!pa || !pb) return pa ? 1 : pb ? -1 : 0;
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] ?? 0) - (pb[i] ?? 0);
    if (d) return Math.sign(d);
  }
  return 0;
}

class Board {
  constructor({ onState, onLog, onClose, onCustom }) {
    this.onState = onState;
    this.onCustom = onCustom;
    this.onLog = onLog;
    this.onClose = onClose;
    this.port = null;
    this.writer = null;
    this.reader = null;
  }

  get connected() { return !!this.port; }

  async open(port) {
    await port.open({ baudRate: 115200 });
    // The USB port can reset the chip through these lines; leave them alone.
    await port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
    this.port = port;
    this.writer = port.writable.getWriter();
    this.readLoop();
  }

  async close() {
    const port = this.port;
    this.port = null;
    try { await this.reader?.cancel(); } catch {}
    try { this.writer?.releaseLock(); } catch {}
    try { await port?.close(); } catch {}
    this.reader = this.writer = null;
  }

  async readLoop() {
    const decoder = new TextDecoder();
    let pending = '';
    try {
      this.reader = this.port.readable.getReader();
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) break;
        pending += decoder.decode(value, { stream: true });
        let nl;
        while ((nl = pending.indexOf('\n')) >= 0) {
          this.onLine(pending.slice(0, nl).trim());
          pending = pending.slice(nl + 1);
        }
      }
    } catch (e) {
      this.onLog(`Read stopped: ${e.message}`);
    } finally {
      try { this.reader?.releaseLock(); } catch {}
    }
    if (this.port) {
      await this.close();
      this.onClose();
    }
  }

  onLine(line) {
    if (line.startsWith('BUSYCUSTOM ')) {
      // A saved custom status's picture: name, hex, then the half-LED offsets.
      const [, name, hex, x, y] = line.split(' ');
      if (hex?.length !== CUSTOM_PIXELS * 4) return;
      const pixels = new Uint16Array(CUSTOM_PIXELS);
      for (let i = 0; i < CUSTOM_PIXELS; i++) pixels[i] = parseInt(hex.slice(i * 4, i * 4 + 4), 16);
      this.onCustom?.(name, pixels, { x: x === '1', y: y === '1' });
      return;
    }
    if (!line.startsWith('BUSY ')) return;
    const state = {};
    for (const part of line.slice(5).split(' ')) {
      const [k, v] = part.split('=');
      state[k] = k === 'names' ? v.split(',') : k === 'fw' ? v : Number(v);
    }
    this.onState(state);
  }

  send(line) {
    return this.writer.write(new TextEncoder().encode(line + '\n'));
  }

  hello() { return this.send('HELLO'); }

  /** Sends this computer's time, and its UTC offset so the board can show local time. */
  syncClock(now = new Date()) {
    const unix = Math.floor(now.getTime() / 1000);
    const offset = -now.getTimezoneOffset() * 60;
    return this.send(`BUSYTIME ${unix} ${offset}`);
  }

  setStatus(index) { return this.send(`STATUS ${index}`); }
  setLayout(index) { return this.send(`LAYOUT ${index}`); }
  clearCustom() { return this.send('CUSTOM CLEAR'); }

  /**
   * [pixels] is 72x16 RGB565 values. Takes about 2 seconds to arrive. [half] moves it half an LED
   * right and/or down; firmware before 1.1.0 can't, so leave it out for those.
   */
  setCustom(pixels, half = null) {
    if (pixels.length !== CUSTOM_PIXELS) throw new Error('A custom status is 72x16');
    let hex = '';
    for (const p of pixels) hex += p.toString(16).padStart(4, '0');
    return this.send(half ? `CUSTOM ${hex} ${half.x ? 1 : 0} ${half.y ? 1 : 0}` : `CUSTOM ${hex}`);
  }

  /** Saves a named custom status (firmware 1.2.0 and later), replacing one of the same name. */
  saveCustom(name, pixels, half) {
    let hex = '';
    for (const p of pixels) hex += p.toString(16).padStart(4, '0');
    return this.send(`CUSTOM ADD ${name} ${hex} ${half.x ? 1 : 0} ${half.y ? 1 : 0}`);
  }

  deleteCustom(name) { return this.send(`CUSTOM DELETE ${name}`); }

  /** Asks for a saved custom status's picture; it arrives through onCustom. */
  getCustom(name) { return this.send(`CUSTOM GET ${name}`); }
}

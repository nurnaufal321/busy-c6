// The BUSY C6 serial protocol: text lines over the board's USB port. See PROTOCOL.md.
'use strict';

const ESPRESSIF_USB_VENDOR = 0x303a;
const CUSTOM_PIXELS = 72 * 16;

class Board {
  constructor({ onState, onLog, onClose }) {
    this.onState = onState;
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
    if (!line.startsWith('BUSY ')) return;
    const state = {};
    for (const part of line.slice(5).split(' ')) {
      const [k, v] = part.split('=');
      state[k] = k === 'names' ? v.split(',') : Number(v);
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

  /** [pixels] is 72x16 RGB565 values. Takes about 2 seconds to arrive. */
  setCustom(pixels) {
    if (pixels.length !== CUSTOM_PIXELS) throw new Error('A custom status is 72x16');
    let hex = '';
    for (const p of pixels) hex += p.toString(16).padStart(4, '0');
    return this.send(`CUSTOM ${hex}`);
  }
}

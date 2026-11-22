// The live preview in the connection card: the board's screen, drawn the way the firmware draws it.
// The status comes from the same status.pack the board has (firmware/tools/pack_status.py,
// main/anim.c), and both views are a port of main/lcd.c.
'use strict';

(() => {
  const LED_W = 72, LED_H = 16, PIXELS = LED_W * LED_H;
  const SW = 320, SH = 170;
  const NAME_LEN = 24, ENTRY = NAME_LEN + 12;
  // Pictures of custom statuses by name, as sent from or read back from the board.
  const CUSTOMS_KEY = 'busy-c6-customs';
  let customs = {};
  try { customs = JSON.parse(localStorage.getItem(CUSTOMS_KEY)) || {}; } catch {}
  const asked = new Set();
  function keep() {
    try { localStorage.setItem(CUSTOMS_KEY, JSON.stringify(customs)); } catch {}
  }
  const CLOCK = 1;
  const page = window.busyPage;
  const canvas = document.querySelector('#live');
  const ctx = canvas.getContext('2d');
  const image = ctx.createImageData(SW, SH);
  const screen = new Uint16Array(SW * SH);

  // ---------- the status animation (main/anim.c) ----------

  let pack = null;
  let loading = null;
  const leds = new Uint16Array(PIXELS);
  let entry = null, start = 0, frame = -1, pos = 0;
  // The custom status's half-LED offset, in screen pixels (half of the 4 px between LEDs).
  let ox = 0, oy = 0;

  function loadPack() {
    loading ??= fetch('firmware-dist/status.pack').then((r) => r.ok ? r.arrayBuffer() : null).then((ab) => {
      if (!ab) return;
      const bytes = new Uint8Array(ab);
      const view = new DataView(ab);
      if (String.fromCharCode(...bytes.subarray(0, 4)) !== 'BSP1') return;
      const entries = new Map();
      for (let i = 0, n = view.getUint32(4, true); i < n; i++) {
        const at = 8 + i * ENTRY;
        const raw = bytes.subarray(at, at + NAME_LEN);
        const end = raw.indexOf(0) < 0 ? NAME_LEN : raw.indexOf(0);
        entries.set(new TextDecoder().decode(raw.subarray(0, end)), {
          fps: view.getUint16(at + NAME_LEN, true),
          frames: view.getUint16(at + NAME_LEN + 2, true),
          offset: view.getUint32(at + NAME_LEN + 4, true),
        });
      }
      pack = { bytes, entries };
      update();
    }).catch(() => {});
  }

  function decodeFrame() {
    const b = pack.bytes;
    let i = 0;
    while (i < PIXELS) {
      const token = b[pos++];
      const n = (token & 0x3f) + 1;
      const type = token >> 6;
      if (type === 1) {
        for (let k = 0; k < n; k++, pos += 2) leds[i + k] = b[pos] | b[pos + 1] << 8;
      } else if (type === 2) {
        leds.fill(b[pos] | b[pos + 1] << 8, i, i + n);
        pos += 2;
      }
      i += n;
    }
  }

  function restart(now) {
    start = now;
    frame = -1;
    pos = entry.offset;
    leds.fill(0);
  }

  /** Brings leds[] up to [now]; returns whether it changed. */
  function advance(now) {
    if (!entry) return false;
    let due = Math.floor((now - start) * entry.fps / 1000);
    if (due >= entry.frames) {
      restart(start + entry.frames * 1000 / entry.fps);
      due = Math.floor((now - start) * entry.fps / 1000) % entry.frames;
    }
    if (due <= frame) return false;
    while (frame < due) { decodeFrame(); frame++; }
    return true;
  }

  // ---------- the views (main/lcd.c) ----------

  const PIC_X = (SW - LED_W * 4) / 2 | 0, PIC_Y = (SH - LED_H * 4) / 2 | 0;
  const CLOCK_PIC_TOP = 4, CLOCK_TEXT_X = 10, CLOCK_TEXT_LEVEL = 178;
  const CLOCK_TIME_BASELINE = 152, CLOCK_DATE1_BASELINE = 121, CLOCK_DATE2_BASELINE = 152;

  const origCols = new Int8Array(SW).fill(-1), origRows = new Int8Array(SH).fill(-1);
  for (let led = 0; led < LED_W; led++) for (let i = 0; i < 3; i++) origCols[PIC_X + led * 4 + i] = led;
  for (let led = 0; led < LED_H; led++) for (let i = 0; i < 3; i++) origRows[PIC_Y + led * 4 + i] = led;
  function drawOriginal() {
    for (let y = 0; y < SH; y++) {
      const row = y >= oy ? origRows[y - oy] : -1;
      for (let x = 0; x < SW; x++) {
        const col = x >= ox ? origCols[x - ox] : -1;
        screen[y * SW + x] = row >= 0 && col >= 0 ? leds[row * LED_W + col] : 0;
      }
    }
  }

  const fonts = {};
  for (const [name, f] of Object.entries(window.CLOCK_FONTS ?? {})) {
    fonts[name] = { glyphs: f.glyphs, bits: Uint8Array.from(atob(f.bits), (ch) => ch.charCodeAt(0)) };
  }
  const textWidth = (font, text) => [...text].reduce((w, ch) => w + (font.glyphs[ch]?.[4] ?? 0), 0);
  function drawText(font, text, x, y, skip) {
    for (const ch of text) {
      const g = font.glyphs[ch];
      if (!g) continue;
      const [w, h, left, top, adv, offset] = g;
      if (ch !== skip) {
        for (let gy = 0; gy < h; gy++) {
          const py = y - top + gy;
          if (py < 0 || py >= SH) continue;
          for (let gx = 0; gx < w; gx++) {
            const px = x + left + gx, a = font.bits[offset + gy * w + gx];
            if (!a || px < 0 || px >= SW) continue;
            const v = a * CLOCK_TEXT_LEVEL >> 8;
            screen[py * SW + px] = (v >> 3) << 11 | (v >> 2) << 5 | v >> 3;
          }
        }
      }
      x += adv;
    }
  }

  function drawClock(clock) {
    screen.fill(0);
    for (let row = 0; row < LED_H; row++) {
      for (let col = 0; col < LED_W; col++) {
        const c = leds[row * LED_W + col];
        for (let line = 0; line < 3; line++) {
          const px = (CLOCK_PIC_TOP + oy + row * 4 + line) * SW + PIC_X + ox + col * 4;
          screen[px] = screen[px + 1] = screen[px + 2] = c;
        }
      }
    }
    if (!fonts.time) return;
    drawText(fonts.date, clock.day, CLOCK_TEXT_X, CLOCK_DATE1_BASELINE);
    drawText(fonts.date, clock.date, CLOCK_TEXT_X, CLOCK_DATE2_BASELINE);
    drawText(fonts.time, clock.time, SW - CLOCK_TEXT_X - textWidth(fonts.time, clock.time), CLOCK_TIME_BASELINE, clock.colon ? '' : ':');
  }

  /** What the board's clock row shows; the page keeps the board on this computer's time. */
  function clockRow(state) {
    const d = new Date();
    const two = (n) => String(n).padStart(2, '0');
    return {
      time: `${two(d.getHours())}:${two(d.getMinutes())}`,
      day: ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'][d.getDay()],
      date: `${d.getDate()} ${['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC'][d.getMonth()]}`,
      colon: !state.time || d.getSeconds() % 2 === 0,
    };
  }

  function paint(layout, state) {
    if (layout === CLOCK) drawClock(clockRow(state));
    else drawOriginal();
    const d = image.data;
    for (let i = 0; i < screen.length; i++) {
      const c = screen[i];
      d[i * 4] = (c >> 11) * 255 / 31;
      d[i * 4 + 1] = (c >> 5 & 63) * 255 / 63;
      d[i * 4 + 2] = (c & 31) * 255 / 31;
      d[i * 4 + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
  }

  // ---------- keeping up with the board ----------

  let shown = null, raf = 0, lastKey = '';

  function tick(now) {
    raf = 0;
    const state = page.state();
    if (!shown || !state) return;
    const changed = advance(now);
    const layout = state.layout;
    // Redraw only when something on the screen changed: a frame or the clock.
    const key = layout === CLOCK ? JSON.stringify(clockRow(state)) : layout;
    if (changed || key !== lastKey) {
      lastKey = key;
      paint(layout, state);
    }
    raf = requestAnimationFrame(tick);
  }

  function update() {
    const state = page.state();
    const name = page.board.connected && state ? state.names?.[state.status] : null;
    document.querySelector('#live-wrap').hidden = !name;
    canvas.setAttribute('aria-label', name ? `The board's screen, showing ${name.replace(/_/g, ' ')}` : '');
    if (!name) { shown = null; return; }
    if (!pack) { loadPack(); return; }
    lastKey = '';
    if (name !== shown) {
      shown = name;
      entry = null;
      ox = oy = 0;
      const custom = state.names.length - (state.custom || 0) <= state.status;
      if (custom) {
        const saved = customs[name];
        leds.set(saved?.pixels?.length === PIXELS ? saved.pixels : new Uint16Array(PIXELS));
        ox = saved?.half?.x ? 2 : 0;
        oy = saved?.half?.y ? 2 : 0;
        // Not seen from this browser: the board can send it (firmware 1.2.0 and later).
        if (!saved && !asked.has(name) && compareVersions(state.fw, SAVED_CUSTOMS_VERSION) >= 0) {
          asked.add(name);
          page.board.getCustom(name);
        }
      } else {
        entry = pack.entries.get(name) ?? null;
        if (entry) restart(performance.now());
        else leds.fill(0);
      }
    }
    if (!raf) raf = requestAnimationFrame(tick);
  }

  function remember(name, pixels, half) {
    customs[name] = { pixels: [...pixels], half: half ?? null };
    keep();
    if (shown === name) { shown = null; update(); }
  }
  page.onCustomSent = remember;
  page.onCustomPicture = remember;
  page.onCustomDeleted = (name) => { delete customs[name]; keep(); };

  // As wide as the custom status preview further down, keeping the screen's shape.
  const custom = document.querySelector('#preview');
  function fit() {
    const w = custom.getBoundingClientRect().width;
    if (!w) return;
    const border = parseFloat(getComputedStyle(canvas).borderTopWidth) * 2;
    canvas.style.width = `${w}px`;
    canvas.style.height = `${(w - border) * SH / SW + border}px`;
  }
  new ResizeObserver(fit).observe(custom);
  page.onRender(() => { update(); fit(); });
})();

// The page: connection, clock, status and view buttons, custom status editor.
'use strict';

const $ = (s) => document.querySelector(s);
const VIEWS = ['Original', 'Clock'];
const SYNC_MS = 60 * 1000;
const W = 72, H = 16, DOT = 10;

function log(msg) {
  const el = $('#log');
  el.textContent += `${new Date().toLocaleTimeString()}  ${msg}\n`;
  el.scrollTop = el.scrollHeight;
}

function setStatus(msg, kind = '') {
  const el = $('#status');
  el.textContent = msg;
  el.classList.toggle('ok', kind === 'ok');
  el.classList.toggle('err', kind === 'err');
}

// ---------- connection ----------

let syncTimer = null;
let lastSync = null;
let state = null;
let flashing = false;
let reconnect = () => {};
const renderHooks = [];

const board = new Board({
  onState: (s) => {
    state = s;
    render();
  },
  onLog: log,
  onCustom: (name, pixels, half) => window.busyPage.onCustomPicture?.(name, pixels, half),
  onClose: () => {
    log('Board disconnected.');
    disconnected();
  },
});

function disconnected() {
  clearInterval(syncTimer);
  syncTimer = null;
  state = null;
  render();
}

async function connectPort(port) {
  try {
    await board.open(port);
  } catch (e) {
    setStatus(`Couldn't open the board: ${e.message}. Something else may be using its port, such as a flashing tool or serial monitor.`, 'err');
    log(`Open failed: ${e.message}`);
    return;
  }
  log('Connected.');
  setStatus('');
  await board.hello();
  await sync();
  syncTimer = setInterval(sync, SYNC_MS);
  render();
}

async function sync() {
  if (!board.connected) return;
  await board.syncClock();
  lastSync = new Date();
  log(`Sent the time: ${lastSync.toLocaleString()}`);
  render();
}

$('#connect').addEventListener('click', async () => {
  if (board.connected) {
    await board.close();
    log('Disconnected.');
    disconnected();
    return;
  }
  try {
    const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: ESPRESSIF_USB_VENDOR }] });
    await connectPort(port);
  } catch (e) {
    if (e.name !== 'NotFoundError') setStatus(e.message, 'err');
  }
});

if ('serial' in navigator) {
  // A board this browser has been allowed to use before connects by itself.
  reconnect = async () => {
    if (board.connected || flashing) return;
    const [port] = await navigator.serial.getPorts();
    if (port) connectPort(port);
  };
  navigator.serial.addEventListener('connect', reconnect);
  reconnect();
} else {
  $('#noserial').hidden = false;
  $('#connect').disabled = true;
}

// What flash.js needs from this page.
window.busyPage = {
  board,
  log,
  state: () => state,
  disconnected,
  reconnect: () => reconnect(),
  setFlashing: (on) => { flashing = on; },
  onRender: (hook) => { renderHooks.push(hook); hook(); },
};

// ---------- status and view ----------

/** A row of choice buttons. [onDelete], if given, adds a delete control to the ones it returns true for. */
function buttons(box, labels, current, onPick, deletable = () => false, onDelete = null) {
  const key = labels.map((l, i) => l + (deletable(i) ? '\u2715' : '')).join('|');
  if (box.dataset.key !== key) {
    box.dataset.key = key;
    box.replaceChildren(...labels.map((label, i) => {
      const b = document.createElement('button');
      b.textContent = label;
      b.addEventListener('click', () => onPick(i));
      if (!deletable(i)) return b;
      const wrap = document.createElement('div');
      wrap.className = 'deletable';
      const del = document.createElement('button');
      del.className = 'delete';
      del.textContent = '\u2715';
      del.title = `Delete ${label}`;
      del.setAttribute('aria-label', `Delete ${label}`);
      del.addEventListener('click', () => onDelete(i));
      wrap.append(b, del);
      return wrap;
    }));
  }
  [...box.children].forEach((el, i) => {
    const b = el.matches('button') ? el : el.querySelector('button');
    if (!b) return;
    b.classList.toggle('on', i === current);
    el.querySelectorAll('button').forEach((x) => { x.disabled = !board.connected; });
  });
}

/** Whether the board keeps several named custom statuses. */
const savesCustoms = () => compareVersions(state?.fw, SAVED_CUSTOMS_VERSION) >= 0;

/** Status [i] is a saved custom one: they come last, and the board says how many there are. */
const isCustom = (i) => !!state && i >= state.names.length - (state.custom || 0);

const pretty = (name) => name.replace(/_/g, ' ').toUpperCase();

function render() {
  const on = board.connected;
  $('#board-name').textContent = on ? 'BUSY C6 board' : 'No board connected';
  const chip = $('#board-status');
  chip.textContent = on ? 'Connected' : 'Not connected';
  chip.classList.toggle('ok', on);
  $('#connect').textContent = on ? 'Disconnect' : 'Connect';
  $('#send').disabled = !on;
  $('#clear').disabled = !on || !state?.custom;
  // Firmware 1.2.0 and later keeps a list: save adds to it, and each one has its own delete.
  $('#send').textContent = !on || savesCustoms() ? 'Save to board' : 'Send to board';
  $('#clear').hidden = on && savesCustoms();

  // The page keeps the clock in sync by itself: on connecting, then every minute.
  const synced = !!state?.time && lastSync;
  const clockChip = $('#clock-chip');
  clockChip.hidden = !on;
  clockChip.classList.toggle('ok', !!synced);
  clockChip.textContent = synced ? `Clock synced ${lastSync.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}` : 'Setting the clock…';

  const names = state?.names ?? [];
  buttons($('#statuses'), names.map(pretty), state?.status, (i) => board.setStatus(i),
    (i) => savesCustoms() && isCustom(i), async (i) => {
      const name = names[i];
      if (!confirm(`Delete the custom status ${pretty(name)} from the board?`)) return;
      await board.deleteCustom(name);
      window.busyPage.onCustomDeleted?.(name);
      log(`Deleted the custom status ${pretty(name)}.`);
      setStatus(`Deleted ${pretty(name)}.`, 'ok');
    });
  if (!names.length) {
    const empty = document.createElement('p');
    empty.className = 'sub empty';
    empty.textContent = 'Connect the board to see its statuses.';
    $('#statuses').replaceChildren(empty);
    delete $('#statuses').dataset.key;
  }
  buttons($('#views'), VIEWS, state?.layout, (i) => board.setLayout(i));
  for (const hook of renderHooks) hook();
}

// ---------- custom status ----------

const FONTS = {
  bold10: { family: 'busy-bold-10', height: 10 },
  bold7: { family: 'busy-bold-7', height: 7 },
  small5: { family: 'busy-regular-5', height: 5 },
};
const sampler = document.createElement('canvas');
sampler.width = W;
sampler.height = H;
const sctx = sampler.getContext('2d', { willReadFrequently: true });
let pixels = new Uint16Array(W * H);

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [n >> 16 & 255, n >> 8 & 255, n & 255];
}

// The built-in statuses sit on a rounded panel, columns 1 to 70, with a rim. Text keeps 2 LEDs
// clear of it on each side, and 1 more for its shadow.
const TEXT_W = 70 - 5;
let customHalf = { x: false, y: false };

/** The largest font that fits; returns null if even the smallest doesn't. */
function pickFont(lines) {
  const fonts = lines.length > 1 ? [FONTS.bold7, FONTS.small5] : [FONTS.bold10, FONTS.bold7, FONTS.small5];
  for (const f of fonts) {
    sctx.font = `16px "${f.family}"`;
    if (lines.every((l) => sctx.measureText(l).width <= TEXT_W + 1)) return f;
  }
  return null;
}

/** Moves a colour toward white (t > 0) or black (t < 0), by |t| from 0 to 1. */
function tone([r, g, b], t) {
  const to = t > 0 ? 255 : 0, k = Math.abs(t);
  return [r + (to - r) * k, g + (to - g) * k, b + (to - b) * k].map(Math.round);
}

/**
 * The colour of each LED in the style of the built-in statuses: a rounded panel whose background
 * darkens towards the bottom, a lighter rim, text that fades a little towards its bottom, and a
 * drop shadow below and to the right of the text.
 */
function styledLed(x, y, lit, bg, fg, ink, panel) {
  const left = panel.x0, right = panel.x1, bottom = panel.y1;
  if (x < left || x > right || y > bottom) return [0, 0, 0];
  // Cut corners, with the LEDs beside them half lit.
  const cx = x === left ? 0 : x === right ? 2 : x === left + 1 ? 1 : x === right - 1 ? 1 : -1;
  const cy = y === 0 || y === bottom ? 0 : y === 1 || y === bottom - 1 ? 1 : -1;
  if (cx === 0 && cy === 0 || cx === 2 && cy === 0) return [0, 0, 0];
  const edge = (cx === 0 || cx === 2) && cy === 1 || cx === 1 && cy === 0;
  let c;
  if (lit(x, y)) {
    c = tone(fg, ink.y1 > ink.y0 ? -0.25 * (y - ink.y0) / (ink.y1 - ink.y0) : 0);
  } else {
    c = tone(bg, 0.15 - 0.45 * y / bottom);
    if (y === bottom) c = tone(bg, -0.55);
    else if (y === 0 || x === left || x === right) c = tone(bg, 0.3);
    if (lit(x - 1, y - 1) || lit(x, y - 1) || lit(x - 1, y)) c = tone(c, -0.6);
  }
  return edge ? tone(c, -0.6) : c;
}

/** Draws the text at 72x16, keeps it on or off per LED, and shows it as dots. */
function drawCustom() {
  const lines = [$('#line1').value, $('#line2').value].map((l) => l.trim().toUpperCase()).filter(Boolean);
  const fg = hexToRgb($('#fg').value), bg = hexToRgb($('#bg').value);
  const font = pickFont(lines);
  sctx.fillStyle = '#000';
  sctx.fillRect(0, 0, W, H);
  $('#fit').textContent = font || !lines.length ? '' : 'Too long to fit. Shorten the text.';
  $('#fit').classList.toggle('err', !font && !!lines.length);
  if (font) {
    sctx.font = `16px "${font.family}"`;
    sctx.fillStyle = '#fff';
    sctx.textBaseline = 'alphabetic';
    sctx.textAlign = 'left';
    const gap = 1;
    const total = lines.length * font.height + (lines.length - 1) * gap;
    let baseline = Math.floor((H - total) / 2) + font.height;
    for (const line of lines) {
      // Whole-pixel positions keep the pixel font's edges sharp.
      sctx.fillText(line, Math.floor((W - sctx.measureText(line).width) / 2), baseline);
      baseline += font.height + gap;
    }
  }
  const data = sctx.getImageData(0, 0, W, H).data;
  const on = (x, y) => x >= 0 && x < W && y >= 0 && y < H && data[(y * W + x) * 4] > 127;
  // Centre on the lit pixels themselves: a font's measured width includes the spacing after the
  // last letter, which would leave the text a pixel or two left of centre.
  let x0 = W, x1 = -1, y0 = H, y1 = -1;
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      if (!on(x, y)) continue;
      x0 = Math.min(x0, x); x1 = Math.max(x1, x);
      y0 = Math.min(y0, y); y1 = Math.max(y1, y);
    }
  }
  // An odd number of blank LEDs can't split evenly, so on firmware that can, the whole picture
  // moves half an LED on the screen, with the panel one LED narrower or shorter to stay centred.
  const state = window.busyPage?.state();
  const canHalf = !state?.fw || compareVersions(state.fw, HALF_LED_VERSION) >= 0;
  // The drop shadow reaches one LED right of and below the letters, and reads as part of them.
  const inkW = x1 - x0 + 2, inkH = y1 - y0 + 2;
  customHalf = { x: canHalf && x1 >= 0 && inkW % 2 === 1, y: canHalf && y1 >= 0 && inkH % 2 === 1 };
  const panel = { x0: 1, x1: customHalf.x ? W - 3 : W - 2, y1: customHalf.y ? H - 2 : H - 1 };
  const cxPanel = (panel.x0 + panel.x1 + 1) / 2, cyPanel = (panel.y1 + 1) / 2;
  const dx = x1 < 0 ? 0 : Math.floor(cxPanel - inkW / 2) - x0;
  const dy = y1 < 0 ? 0 : Math.floor(cyPanel - inkH / 2) - y0;
  const out = $('#preview').getContext('2d');
  out.fillStyle = '#000';
  out.fillRect(0, 0, W * DOT, H * DOT);
  const lit = (x, y) => on(x - dx, y - dy);
  const ink = { y0: y0 + dy, y1: y1 + dy };
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const [r, g, b] = styledLed(x, y, lit, bg, fg, ink, panel);
      pixels[y * W + x] = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
      out.fillStyle = `rgb(${r},${g},${b})`;
      out.fillRect(x * DOT + (customHalf.x ? DOT / 2 : 0), y * DOT + (customHalf.y ? DOT / 2 : 0), DOT - 2, DOT - 2);
    }
  }
  return !!font;
}

for (const id of ['#line1', '#line2', '#fg', '#bg']) $(id).addEventListener('input', drawCustom);

/** The board's name for a design: its text in capitals, words joined by _, e.g. BACK_AT_3. */
function customName() {
  const text = [$('#line1').value, $('#line2').value].join(' ').toUpperCase();
  return text.replace(/[^A-Z0-9]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 23).replace(/_+$/, '') || 'CUSTOM';
}

$('#send').addEventListener('click', async () => {
  if (!drawCustom()) return;
  if (!savesCustoms()) {
    // Firmware before 1.2.0 holds one custom status.
    setStatus('Sending the custom status…');
    const canHalf = compareVersions(state?.fw, HALF_LED_VERSION) >= 0;
    const half = canHalf ? customHalf : { x: false, y: false };
    await board.setCustom(pixels, canHalf ? customHalf : null);
    window.busyPage.onCustomSent?.('custom', pixels, half);
    log('Sent a custom status.');
    setStatus('Custom status sent. It\'s now last in the swipe order.', 'ok');
    return;
  }
  const name = customName();
  const exists = state?.names?.includes(name);
  if (!exists && (state?.custom || 0) >= CUSTOM_MAX) {
    setStatus(`The board holds ${CUSTOM_MAX} custom statuses. Delete one in the Status list first.`, 'err');
    return;
  }
  setStatus(`Saving ${pretty(name)}…`);
  await board.saveCustom(name, pixels, customHalf);
  window.busyPage.onCustomSent?.(name, pixels, customHalf);
  log(`${exists ? 'Replaced' : 'Saved'} the custom status ${pretty(name)}.`);
  setStatus(`${exists ? 'Replaced' : 'Saved'} ${pretty(name)}. It's in the Status list and the swipe order.`, 'ok');
});

$('#clear').addEventListener('click', async () => {
  await board.clearCustom();
  log('Cleared the custom status.');
  setStatus('Custom status cleared.', 'ok');
});

// The pixel fonts load on demand, so ask for them before the first draw.
const fontFaces = [
  new FontFace('busy-bold-10', 'url(fonts/busy_bold_10px.ttf)'),
  new FontFace('busy-bold-7', 'url(fonts/busy_bold_7px.ttf)'),
  new FontFace('busy-regular-5', 'url(fonts/busy_regular_5px.ttf)'),
];
Promise.all(fontFaces.map((f) => f.load())).then((faces) => {
  faces.forEach((f) => document.fonts.add(f));
  drawCustom();
}).catch((e) => log(`Couldn't load the fonts: ${e.message}`));

render();

// The page: connection, clock, status and view buttons, custom status editor.
'use strict';

const $ = (s) => document.querySelector(s);
const VIEWS = ['Original', 'A. Tall', 'B. Pan', 'C. Glow', 'D. Clock'];
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

const board = new Board({
  onState: (s) => {
    state = s;
    render();
  },
  onLog: log,
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

$('#sync').addEventListener('click', async () => {
  await sync();
  setStatus('Clock synced.', 'ok');
});

if ('serial' in navigator) {
  // A board this browser has been allowed to use before connects by itself.
  const reconnect = async () => {
    if (board.connected) return;
    const [port] = await navigator.serial.getPorts();
    if (port) connectPort(port);
  };
  navigator.serial.addEventListener('connect', reconnect);
  reconnect();
} else {
  $('#noserial').hidden = false;
  $('#connect').disabled = true;
}

// ---------- status and view ----------

function buttons(box, labels, current, onPick) {
  if (box.querySelectorAll('button').length !== labels.length || [...box.children].some((b, i) => b.textContent !== labels[i])) {
    box.replaceChildren(...labels.map((label, i) => {
      const b = document.createElement('button');
      b.textContent = label;
      b.addEventListener('click', () => onPick(i));
      return b;
    }));
  }
  [...box.children].forEach((b, i) => {
    b.classList.toggle('on', i === current);
    b.disabled = !board.connected;
  });
}

const pretty = (name) => name.replace(/_/g, ' ').toUpperCase();

function render() {
  const on = board.connected;
  $('#board-name').textContent = on ? 'BUSY C6 board' : 'No board connected';
  const chip = $('#board-status');
  chip.textContent = on ? 'Connected' : 'Not connected';
  chip.classList.toggle('ok', on);
  $('#connect').textContent = on ? 'Disconnect' : 'Connect';
  $('#sync').disabled = !on;
  $('#send').disabled = !on;
  $('#clear').disabled = !on || !state?.custom;

  const timeSet = !!state?.time;
  $('#summary').textContent = !on ? 'Connect the board to set its clock.'
    : timeSet && lastSync ? `Clock synced at ${lastSync.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}` : 'Setting the clock…';

  const names = state?.names ?? [];
  buttons($('#statuses'), names.map(pretty), state?.status, (i) => board.setStatus(i));
  if (!names.length) {
    const empty = document.createElement('p');
    empty.className = 'sub empty';
    empty.textContent = 'Connect the board to see its statuses.';
    $('#statuses').replaceChildren(empty);
  }
  buttons($('#views'), VIEWS, state?.layout, (i) => board.setLayout(i));
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

/** The largest font that fits; returns null if even the smallest doesn't. */
function pickFont(lines) {
  const fonts = lines.length > 1 ? [FONTS.bold7, FONTS.small5] : [FONTS.bold10, FONTS.bold7, FONTS.small5];
  for (const f of fonts) {
    sctx.font = `16px "${f.family}"`;
    if (lines.every((l) => sctx.measureText(l).width <= W)) return f;
  }
  return null;
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
  const out = $('#preview').getContext('2d');
  out.fillStyle = '#000';
  out.fillRect(0, 0, W * DOT, H * DOT);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const lit = data[(y * W + x) * 4] > 127;
      const [r, g, b] = lit ? fg : bg;
      pixels[y * W + x] = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
      out.fillStyle = `rgb(${r},${g},${b})`;
      out.fillRect(x * DOT, y * DOT, DOT - 2, DOT - 2);
    }
  }
  return !!font;
}

for (const id of ['#line1', '#line2', '#fg', '#bg']) $(id).addEventListener('input', drawCustom);

$('#send').addEventListener('click', async () => {
  if (!drawCustom()) return;
  setStatus('Sending the custom status…');
  await board.setCustom(pixels);
  log('Sent a custom status.');
  setStatus('Custom status sent. It\'s now last in the swipe order.', 'ok');
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

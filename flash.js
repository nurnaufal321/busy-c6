// The firmware card: flashes the build published next to this page (firmware-dist/), over Web Serial.
// esptool-js is loaded only when flashing. The nvs partition is never written, so the board keeps
// its status, view, clock offset and custom status.

const ESPTOOL_URL = 'https://cdn.jsdelivr.net/npm/esptool-js@0.7.0/bundle.js';
const DIST = 'firmware-dist/';
const UPDATE_ROLES = ['app', 'status'];
const FULL_ROLES = ['bootloader', 'partitions', 'app', 'status'];

const $ = (s) => document.querySelector(s);
const page = window.busyPage;
let manifest = null;
let busy = false;

function say(msg, kind = '') {
  const el = $('#fw-status');
  el.textContent = msg;
  el.classList.toggle('ok', kind === 'ok');
  el.classList.toggle('err', kind === 'err');
}

function render() {
  const on = page.board.connected;
  const fw = on ? page.state()?.fw : null;
  const latest = manifest?.version;
  // Before the board answers, there's nothing to compare yet.
  const known = on && !!page.state();
  const order = known && latest ? compareVersions(fw, latest) : null;

  const chip = $('#fw-chip');
  chip.hidden = !known;
  chip.classList.toggle('ok', order === 0);
  chip.classList.toggle('warn', order === -1);
  chip.textContent = order === 0 ? `Firmware ${fw} · up to date`
    : order === -1 ? `Update available: ${latest}`
    : fw ? `Firmware ${fw}` : 'Firmware: unknown';

  // Version numbers in bold, so two of them never read as one string.
  const v = (text) => Object.assign(document.createElement('strong'), { textContent: text });
  $('#fw-summary').replaceChildren(...(!manifest ? ['No firmware is published with this copy of the page.']
    : !on ? ['Latest firmware: version ', v(latest), '. Connect the board to check its version.']
    : !known ? ['Latest firmware: version ', v(latest), '.']
    : order === 0 ? ['Your board is up to date, on version ', v(fw), '.']
    : order === -1 ? ['Version ', v(latest), ' is available. What\'s new:']
    : ['Your board has version ', v(fw), ', newer than the published version ', v(latest), '.']));
  // With an update waiting, the corner says what the board has now.
  $('#fw-meta').textContent = order === -1 ? `Your board: ${fw || 'before 1.0.0'}` : 'Updates over the USB cable';
  const notes = order === -1 ? manifest.notes ?? [] : [];
  $('#fw-notes').replaceChildren(...notes.map((n) => Object.assign(document.createElement('li'), { textContent: n })));
  $('#fw-notes').hidden = !notes.length;

  const supported = 'serial' in navigator;
  $('#fw-update').disabled = busy || !manifest || !on || !supported;
  $('#fw-update').classList.toggle('primary', order === -1);
  $('#fw-update').textContent = order === -1 ? `Update to ${latest}` : 'Reinstall';
  $('#fw-full').disabled = busy || !manifest || !supported;
}

async function loadManifest() {
  try {
    const res = await fetch(DIST + 'manifest.json', { cache: 'no-cache' });
    if (res.ok) manifest = await res.json();
  } catch {}
  render();
}

const hex = (buf) => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');

/** Downloads the parts and checks them against the manifest. */
async function download(roles) {
  const parts = manifest.files.filter((f) => roles.includes(f.role));
  const out = [];
  for (const part of parts) {
    say(`Downloading ${part.file}…`);
    const res = await fetch(DIST + part.file, { cache: 'no-cache' });
    if (!res.ok) throw new Error(`couldn't download ${part.file} (${res.status})`);
    const data = new Uint8Array(await res.arrayBuffer());
    if (hex(await crypto.subtle.digest('SHA-256', data)) !== part.sha256) {
      throw new Error(`${part.file} didn't match the manifest. Reload the page and try again.`);
    }
    out.push({ ...part, data });
  }
  return out;
}

async function flash(roles, label) {
  if (busy) return;
  busy = true;
  page.setFlashing(true);
  render();
  const progress = $('#fw-progress');
  let transport = null;
  try {
    const parts = await download(roles);
    let port = page.board.port;
    if (port) {
      await page.board.close();
      page.disconnected();
    } else {
      port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
    }
    say('Loading the flasher…');
    const { ESPLoader, Transport } = await import(ESPTOOL_URL);
    transport = new Transport(port, false);
    const terminal = { clean() {}, write() {}, writeLine: (line) => page.log(`flasher: ${line}`) };
    const loader = new ESPLoader({ transport, baudrate: 921600, terminal });
    say('Putting the board into download mode…');
    const chip = await loader.main();
    page.log(`Flasher found ${chip}.`);
    if (!/ESP32-C6/i.test(chip)) throw new Error(`this is an ${chip}, not an ESP32-C6`);

    let next = 0;
    progress.hidden = false;
    progress.value = 0;
    say(`Writing ${parts.map((p) => p.file).join(', ')}…`);
    await loader.writeFlash({
      fileArray: parts.map((p) => ({ data: p.data, address: p.offset })),
      flashMode: 'keep',
      flashFreq: 'keep',
      flashSize: 'keep',
      eraseAll: false,
      compress: true,
      reportProgress: (i, written, total) => { progress.value = (i + written / total) / parts.length; },
      // The flasher compares this with the board's own MD5 of what it wrote.
      // Called once per file, in order.
      calculateMD5Hash: () => parts[next++].md5,
    });
    progress.value = 1;
    say('Restarting the board…');
    await loader.after('hard_reset');
    await transport.disconnect().catch(() => {});
    transport = null;
    page.log(`${label} done: ${manifest.version}.`);
    say(`${label} done. The board is running ${manifest.version}.`, 'ok');
  } catch (e) {
    if (e.name === 'NotFoundError') {
      say('');
    } else {
      page.log(`Flashing failed: ${e.message}`);
      say(`Flashing failed: ${e.message}. If the board isn't found, hold BOOT, tap RESET, let go of BOOT, then use Full install.`, 'err');
    }
    if (transport) await transport.disconnect().catch(() => {});
  } finally {
    progress.hidden = true;
    busy = false;
    page.setFlashing(false);
    render();
    // Give the board a moment to come back on USB, then talk to it again.
    setTimeout(page.reconnect, 1500);
  }
}

$('#fw-update').addEventListener('click', () => flash(UPDATE_ROLES, 'Update'));
$('#fw-full').addEventListener('click', () => {
  if (confirm('Full install rewrites the bootloader, partition table, firmware and animations. Settings and the custom status are kept. Go ahead?')) {
    flash(FULL_ROLES, 'Full install');
  }
});
page.onRender(render);
loadManifest();

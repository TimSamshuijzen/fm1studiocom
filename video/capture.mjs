// capture.mjs - records the screen takes of the promotional video: FM-1 Studio in Chrome, played by a script.
// README.md in this folder tells how to work with it.
//
//   node video/capture.mjs screen               record this take to footage/screen/
//   node video/capture.mjs --all                record all takes
//   node video/capture.mjs --show screen        with a visible browser window (to watch a take)
//   node video/capture.mjs --list               the takes
//
// Chrome runs with a profile of its own in a temporary folder, without a window (unless --show), and gets the
// page from a small web server on this computer that serves ../src (or --url). A take is a script of clicks,
// typing, keys and knobs, with a cursor drawn in the page (a browser without a window has no mouse pointer).
// The page records itself: the picture of its tab (tab capture) and the sound of the FM-1 simulator (its audio
// node), so nothing plays on the speakers. The clipboard of the computer is not used.
// Each take gives footage/screen/<take>.mkv (H.264, 30 frames a second, FLAC) and <take>.marks.json: the
// times of its marks, which timeline.json can use as "in" (see README.md).

import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const SRC = resolve(HERE, '..', 'src');
const FPS = 30;
const WIDTH = 1920, HEIGHT = 1080;
const NIGHT_DRIVE = join(HERE, 'night-drive', 'main.c');

// ---- Tools ----

function findChrome() {
  const places = [process.env.CHROME,
    'C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
    `${process.env.LOCALAPPDATA}/Google/Chrome/Application/chrome.exe`,
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
    '/usr/bin/google-chrome', '/usr/bin/chromium', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'];
  const found = places.find((path) => path && existsSync(path));
  if (!found) throw new Error('Chrome or Edge is not found: set CHROME to its path');
  return found;
}

function findFfmpeg() {
  if (process.env.FFMPEG) return process.env.FFMPEG;
  const probe = spawnSync('ffmpeg', ['-version']);
  if (!probe.error) return 'ffmpeg';
  const root = join(process.env.LOCALAPPDATA ?? '', 'Microsoft', 'WinGet', 'Packages');
  if (existsSync(root)) {  // where winget puts it, before a new shell has it on the PATH
    const hit = spawnSync('cmd', ['/c', 'dir', '/b', '/s', join(root, 'ffmpeg.exe')], { encoding: 'utf8' }).stdout.split(/\r?\n/)
      .find((line) => /FFmpeg/i.test(line) && line.endsWith('ffmpeg.exe'));
    if (hit) return hit;
  }
  throw new Error('ffmpeg is not found: install it (winget install --id Gyan.FFmpeg.Essentials -e) or set FFMPEG');
}

const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

// A web server for ../src on a free port of 127.0.0.1, as `python -m http.server` in README.md.
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.json': 'application/json',
  '.wasm': 'application/wasm', '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml', '.txt': 'text/plain' };
function serve() {
  const server = createServer((request, response) => {
    let path = decodeURIComponent(new URL(request.url, 'http://x').pathname);
    if (path.endsWith('/')) path += 'index.html';
    // /video/... is this folder (the page of the AI chat, ai-chat.html), all other paths are ../src
    const root = path.startsWith('/video/') ? HERE : SRC;
    const file = resolve(root, '.' + (root === HERE ? path.slice(6) : path));
    if (!file.startsWith(root) || !existsSync(file) || statSync(file).isDirectory()) {
      response.writeHead(404).end();
      return;
    }
    response.writeHead(200, { 'Content-Type': TYPES[extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
    response.end(readFileSync(file));
  });
  return new Promise((done) => server.listen(0, '127.0.0.1', () => done(server)));
}

// ---- Chrome over the DevTools protocol ----

class Browser {
  async start(show) {
    this.profile = mkdtempSync(join(tmpdir(), 'fm1studio-capture-'));
    const args = [`--user-data-dir=${this.profile}`, '--remote-debugging-port=0', '--no-first-run', '--no-default-browser-check',
      '--disable-extensions', '--disable-sync', '--disable-features=Translate,MediaRouter', '--autoplay-policy=no-user-gesture-required',
      // the window of Chrome without a window has a border of 16 x 151 pixels: this gives 1920 x 1080 inside it
      '--auto-accept-this-tab-capture', `--window-size=${WIDTH + 16},${HEIGHT + 151}`, '--force-device-scale-factor=1', '--hide-scrollbars',
      // without a window, Chrome has a screen of 800 x 600: a larger one, so that the window and the capture fit
      '--mute-audio', ...(show ? [] : ['--headless=new', '--screen-info={0,0 3840x2160}']), 'about:blank'];
    this.process = spawn(findChrome(), args, { stdio: 'ignore' });
    const portFile = join(this.profile, 'DevToolsActivePort');
    for (let i = 0; i < 200 && !existsSync(portFile); i++) await sleep(100);
    const port = readFileSync(portFile, 'utf8').split('\n')[0];
    const page = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find((target) => target.type === 'page');
    this.socket = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((done) => this.socket.addEventListener('open', done));
    this.next = 0;
    this.waiting = new Map();
    this.listeners = [];
    this.socket.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      if (message.id && this.waiting.has(message.id)) {
        const [done, fail] = this.waiting.get(message.id);
        this.waiting.delete(message.id);
        if (message.error) fail(new Error(`${message.error.message}`));
        else done(message.result);
      } else if (message.method) {
        for (const listen of this.listeners) listen(message);
      }
    });
    await this.send('Page.enable');
    await this.send('Runtime.enable');
    await this.send('Runtime.addBinding', { name: 'capOut' });  // the page gives the recording to this script with it
    this.on('Runtime.exceptionThrown', (params) => console.log('  page error:', params.exceptionDetails?.exception?.description?.split('\n')[0]));
  }

  // Tab capture records the inside of the window: it must be 1920 x 1080. The window gets the size that gives
  // that, with its border as it is now (Chrome shows a bar "sharing this tab" in it while a capture runs).
  async fitWindow() {
    for (let tries = 0; tries < 3; tries++) {
      const [inner, outer] = (await this.send('Runtime.evaluate', { expression: '[[innerWidth, innerHeight], [outerWidth, outerHeight]]',
        returnByValue: true })).result.value;
      if (process.env.CAPTURE_DEBUG) console.log('  window', JSON.stringify({ inner, outer }));
      if (inner[0] === WIDTH && inner[1] === HEIGHT) break;
      const { windowId } = await this.send('Browser.getWindowForTarget');
      await this.send('Browser.setWindowBounds', { windowId, bounds: { windowState: 'normal',
        width: outer[0] + WIDTH - inner[0], height: outer[1] + HEIGHT - inner[1] } });
      await sleep(400);
      if (tries === 2) console.log(`  note: the inside of the window is ${inner.join(' x ')}, not ${WIDTH} x ${HEIGHT}`);
    }
  }

  send(method, params = {}) {
    return new Promise((done, fail) => {
      const id = ++this.next;
      this.waiting.set(id, [done, fail]);
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }

  on(method, listen) {
    const wrapped = (message) => { if (message.method === method) listen(message.params); };
    this.listeners.push(wrapped);
    return () => { this.listeners = this.listeners.filter((other) => other !== wrapped); };
  }

  // Runs `expression` in the page (as a user gesture, so that a recording may start) and gives its value.
  async eval(expression) {
    const result = await this.send('Runtime.evaluate', { expression, awaitPromise: true, userGesture: true, returnByValue: true });
    if (result.exceptionDetails) throw new Error(`in the page: ${result.exceptionDetails.exception?.description ?? result.exceptionDetails.text}`);
    return result.result.value;
  }

  async waitFor(expression, seconds = 60, what = expression) {
    for (const end = Date.now() + seconds * 1000; Date.now() < end; await sleep(200)) {
      if (await this.eval(`(() => { try { return !!(${expression}); } catch { return false; } })()`)) return;
    }
    throw new Error(`waited ${seconds} s for: ${what}`);
  }

  async goto(url) {
    const loaded = new Promise((done) => { const off = this.on('Page.loadEventFired', () => { off(); done(); }); });
    await this.send('Page.navigate', { url });
    await loaded;
  }

  async stop() {
    try { this.socket.close(); } catch {}
    this.process.kill();
    for (let i = 0; i < 20; i++) {
      try { rmSync(this.profile, { recursive: true, force: true }); return; } catch { await sleep(250); }
    }
  }
}

// ---- What a take does in the page: the cursor, the key caps and the recorder ----

const PAGE_TOOLS = String.raw`(() => {
  if (window.cap) return;
  const style = document.createElement('style');
  style.textContent = '#cap-cursor, #cap-keys { position: fixed; inset: auto; margin: 0; padding: 0; border: 0; background: none; ' +
    'overflow: visible; pointer-events: none; color: inherit; } ' +
    '#cap-cursor { left: 0; top: 0; width: 1px; height: 1px; will-change: transform; } ' +
    '#cap-cursor svg { position: absolute; left: -3px; top: -2px; width: 30px; height: 40px; filter: drop-shadow(0 2px 3px #000a); } ' +
    '#cap-cursor .ring { position: absolute; left: -26px; top: -26px; width: 52px; height: 52px; border-radius: 50%; ' +
    'border: 3px solid #ffdf76; opacity: 0; } ' +
    '#cap-cursor .ring.on { animation: cap-ring 0.45s ease-out; } ' +
    '@keyframes cap-ring { from { transform: scale(0.3); opacity: 0.95; } to { transform: scale(1.25); opacity: 0; } } ' +
    '#cap-keys { left: 50%; top: 120px; transform: translateX(-50%); display: flex; gap: 10px; opacity: 0; transition: opacity 0.15s; } ' +
    '#cap-keys.on { opacity: 1; } ' +
    '#cap-keys kbd { font: 700 34px/1 Arial, sans-serif; color: #100f0e; background: #ede4cc; border-radius: 10px; ' +
    'padding: 14px 22px 12px; box-shadow: 0 5px 0 #a89f8b, 0 8px 18px #000a; }';
  document.head.append(style);
  const cursor = document.createElement('div');
  cursor.id = 'cap-cursor';
  cursor.popover = 'manual';
  cursor.innerHTML = '<i class="ring"></i><svg viewBox="0 0 24 32"><path d="M2 2v24l6.5-6 4.2 9.5 4-1.8-4.2-9.3H21z" ' +
    'fill="#fff" stroke="#000" stroke-width="1.6" stroke-linejoin="round"/></svg>';
  const keys = document.createElement('div');
  keys.id = 'cap-keys';
  keys.popover = 'manual';
  document.body.append(cursor, keys);
  let x = innerWidth * 0.62, y = innerHeight * 0.55;
  const place = () => { cursor.style.transform = 'translate(' + x + 'px, ' + y + 'px)'; };
  place();
  const ease = (u) => (u < 0.5 ? 4 * u * u * u : 1 - Math.pow(-2 * u + 2, 3) / 2);
  window.cap = {
    // The cursor and the key caps stay above the dialogs: a dialog that opens comes on top, so they go on top again.
    top() {
      for (const part of [cursor, keys]) { if (part.matches(':popover-open')) part.hidePopover(); part.showPopover(); }
    },
    move(toX, toY, ms) {
      const fromX = x, fromY = y, start = performance.now();
      return new Promise((done) => {
        const step = (now) => {
          const u = Math.min(1, (now - start) / ms), e = ease(u);
          x = fromX + (toX - fromX) * e;
          y = fromY + (toY - fromY) * e;
          place();
          if (u < 1) requestAnimationFrame(step); else done();
        };
        requestAnimationFrame(step);
      });
    },
    ring() {
      const ring = cursor.querySelector('.ring');
      ring.classList.remove('on');
      void ring.offsetWidth;
      ring.classList.add('on');
    },
    keys(names, ms, at) {
      keys.style.left = at ? at[0] + 'px' : '';
      keys.style.top = at ? at[1] + 'px' : '';
      keys.replaceChildren(...names.map((name) => Object.assign(document.createElement('kbd'), { textContent: name })));
      keys.classList.add('on');
      clearTimeout(keys.timer);
      keys.timer = setTimeout(() => keys.classList.remove('on'), ms);
    },
    center(selector) {
      const element = typeof selector === 'string' ? document.querySelector(selector) : selector;
      if (!element) throw new Error('not in the page: ' + selector);
      const box = element.getBoundingClientRect();
      return [box.x + box.width / 2, box.y + box.height / 2];
    },
    // The recording: the picture of this tab, and the sound of the FM-1 simulator from its audio node. share()
    // starts the capture of the tab; record() starts the recording, after capture.mjs gave the window its size.
    async share() {
      this.stream = await navigator.mediaDevices.getDisplayMedia({ preferCurrentTab: true, audio: false,
        video: { frameRate: ${FPS}, width: ${WIDTH}, height: ${HEIGHT} } });
    },
    async record() {
      const stream = this.stream;
      let tap = null;  // a page that is not FM-1 Studio has no sound
      if (typeof startAudio === 'function') {
        startAudio();
        await audioReady;
        tap = audioContext.createMediaStreamDestination();
        fm1.connect(tap);
      }
      const type = ['video/webm;codecs=vp9,opus', 'video/webm;codecs=vp8,opus'].find((t) => MediaRecorder.isTypeSupported(t));
      const recorder = new MediaRecorder(new MediaStream([...stream.getVideoTracks(), ...(tap ? tap.stream.getAudioTracks() : [])]),
        { mimeType: type, videoBitsPerSecond: 24e6, audioBitsPerSecond: 320e3 });
      // Each second of the recording goes to capture.mjs, in sequence, as base64 (the binding capOut).
      let sent = Promise.resolve(), bytes = 0;
      recorder.ondataavailable = (event) => {
        sent = sent.then(async () => {
          const data = new Uint8Array(await event.data.arrayBuffer());
          bytes += data.length;
          let text = '';
          for (let i = 0; i < data.length; i += 0x8000) text += String.fromCharCode(...data.subarray(i, i + 0x8000));
          capOut(btoa(text));
        });
      };
      await new Promise((done) => { recorder.onstart = done; recorder.start(1000); });
      this.recording = { recorder, stream, tap, start: performance.now(), sent: () => sent, bytes: () => bytes };
      return type;
    },
    mark() { return (performance.now() - this.recording.start) / 1000; },
    async stop() {
      const { recorder, stream, tap } = this.recording;
      await new Promise((done) => { recorder.onstop = done; recorder.stop(); });
      stream.getTracks().forEach((track) => track.stop());
      if (tap) fm1.disconnect(tap);
      await this.recording.sent();
      capOut('END');
      return this.recording.bytes();
    },
  };
  window.cap.top();
})()`;

// ---- A take: the actions that its script uses ----

class Take {
  constructor(browser, name) {
    this.b = browser;
    this.name = name;
    this.marks = {};
  }

  wait(seconds) { return sleep(seconds * 1000); }

  async tools() { await this.b.eval(PAGE_TOOLS); }

  // The centre of an element (a CSS selector, or a page expression that gives an element) or [x, y].
  async point(target) {
    if (Array.isArray(target)) return target;
    if (target.startsWith('js:')) return this.b.eval(`cap.center(${target.slice(3)})`);
    return this.b.eval(`cap.center(${JSON.stringify(target)})`);
  }

  // Moves the cursor to `target` in `seconds` (the real mouse goes along, for the hover effects).
  async move(target, seconds = 0.6) {
    const [x, y] = await this.point(target);
    await this.b.eval(`cap.move(${x}, ${y}, ${seconds * 1000})`);
    await this.b.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
    return [x, y];
  }

  async click(target, { seconds = 0.6, hold = 0.09, pause = 0.25 } = {}) {
    const [x, y] = await this.move(target, seconds);
    await this.wait(0.08);
    await this.b.eval('cap.ring()');
    await this.b.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount: 1 });
    await this.wait(hold);
    await this.b.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', buttons: 0, clickCount: 1 });
    await this.b.eval('cap.top()');
    await this.wait(pause);
  }

  // Types text where the focus is, one character at a time, at about `perSecond` characters a second.
  async type(text, perSecond = 14) {
    for (const char of text) {
      await this.b.send('Input.insertText', { text: char });
      const base = 1 / perSecond;
      await this.wait(base * (0.55 + Math.random() * 0.9) + (char === ' ' ? base * 0.4 : 0) + ('.,;:'.includes(char) ? base * 2 : 0));
    }
  }

  // A key of the computer, by its code ("KeyA", "Semicolon"): down, held for `seconds`, up.
  async keyDown(code) { await this.b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...keyOf(code) }); }
  async keyUp(code) { await this.b.send('Input.dispatchKeyEvent', { type: 'keyUp', ...keyOf(code) }); }
  async hold(codes, seconds) {
    for (const code of codes) { await this.keyDown(code); await this.wait(0.012); }
    await this.wait(seconds);
    for (const code of codes) await this.keyUp(code);
  }

  // Ctrl+A in the focused field, with an editing command, as a user selects all.
  async selectAll() {
    await this.b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', modifiers: 2, key: 'a', code: 'KeyA',
      windowsVirtualKeyCode: 65, commands: ['selectAll'] });
    await this.b.send('Input.dispatchKeyEvent', { type: 'keyUp', modifiers: 2, key: 'a', code: 'KeyA', windowsVirtualKeyCode: 65 });
  }

  // Turns a knob of the simulator with the mouse wheel: + is clockwise. One step each `seconds`.
  async turn(target, steps, seconds = 0.07) {
    const [x, y] = await this.move(target, 0.5);
    for (let i = 0; i < Math.abs(steps); i++) {
      await this.b.send('Input.dispatchMouseEvent', { type: 'mouseWheel', x, y, deltaX: 0, deltaY: steps > 0 ? -100 : 100 });
      await this.wait(seconds);
    }
  }

  // Turns the mouse wheel over `target`: + is down. One step each `seconds`.
  async wheel(target, steps, seconds = 0.09) {
    const [x, y] = await this.move(target, 0.6);
    for (let i = 0; i < Math.abs(steps); i++) {
      await this.b.send('Input.dispatchMouseEvent', { type: 'mouseWheel', x, y, deltaX: 0, deltaY: steps > 0 ? 100 : -100 });
      await this.wait(seconds);
    }
  }

  // A double click: it selects the word there.
  async doubleClick(target, seconds = 0.7) {
    const [x, y] = await this.move(target, seconds);
    await this.b.eval('cap.ring()');
    for (const clickCount of [1, 2]) {
      await this.b.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount });
      await this.wait(0.05);
      await this.b.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', buttons: 0, clickCount });
      await this.wait(0.07);
    }
  }

  // Ctrl+Enter, with its key caps at `at` [x, y] (or at their usual place).
  async ctrlEnter(at) {
    await this.b.eval(`cap.keys(['Ctrl', 'Enter'], 1100, ${JSON.stringify(at ?? null)})`);
    await this.wait(0.25);
    const key = { modifiers: 2, key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 };
    await this.b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...key });
    await this.b.send('Input.dispatchKeyEvent', { type: 'keyUp', ...key });
  }

  // Ctrl+V of `text` on the page: the key caps show, and the page gets a paste event with the text. (The clipboard
  // of the computer stays as it is.)
  async paste(text) {
    await this.b.eval(`cap.keys(['Ctrl', 'V'], 900)`);
    await this.wait(0.25);
    await this.b.eval(`(() => {
      const data = new DataTransfer();
      data.setData('text/plain', ${JSON.stringify(text)});
      document.dispatchEvent(new ClipboardEvent('paste', { clipboardData: data, bubbles: true, cancelable: true }));
    })()`);
  }

  async mark(name) {
    if (!/^[a-z][a-z0-9_]*$/.test(name)) throw new Error(`a mark is lower case, digits and _: ${name}`);
    this.marks[name] = Math.round((await this.b.eval('cap.mark()')) * 1000) / 1000;
  }
}

// The computer keys that the takes use: the notes of FM-1 Studio (A W S E D F T G Y H U J K O L P ; ') and Z, X.
function keyOf(code) {
  const letter = /^Key([A-Z])$/.exec(code);
  if (letter) return { code, key: letter[1].toLowerCase(), windowsVirtualKeyCode: letter[1].charCodeAt(0) };
  const other = { Semicolon: [';', 186], Quote: ["'", 222] }[code];
  if (other) return { code, key: other[0], windowsVirtualKeyCode: other[1] };
  throw new Error(`no key ${code}`);
}

// ---- The takes ----
// Each take starts on a fresh page with the demo running, and records until it returns.
// The marks are the moments that timeline.json picks.

const PANEL = {  // the parts of the simulator, as expressions that give an element
  knob: (n) => `js:document.querySelectorAll('#panel .knob')[${n}]`,  // 0 MASTER, 1 SELECT, 2 PRESETS, 3 ALGORITHM, 4-7 KNOB1-4
  key: (id) => `js:keyElements[${id}]`,                                // ids as in fm1.h: 12 is PLAY, 14 + n is note key n
  body: [1385, 640],                                                   // the panel between the trays: no control there
};

// The AI chat of the take: ai-chat.html over the whole page, as another tab of the browser in front. It is in the
// top layer, so that it is above the page; the take closes the VIBE dialog first (a dialog would keep it from clicks).
const CHAT = {
  frame: `document.getElementById('cap-chat').firstChild`,
  part: (selector) => `js:document.getElementById('cap-chat').firstChild.contentDocument.querySelector('${selector}')`,
  open: `new Promise((done) => {
    const tab = Object.assign(document.createElement('div'), { id: 'cap-chat', popover: 'manual' });
    tab.style.cssText = 'position: fixed; inset: 0; width: 100%; height: 100%; margin: 0; padding: 0; border: 0; background: #171615;';
    const frame = Object.assign(document.createElement('iframe'), { src: 'video/ai-chat.html' });
    frame.style.cssText = 'width: 100%; height: 100%; border: 0; display: block;';
    frame.onload = () => { tab.showPopover(); cap.top(); done(); };
    tab.append(frame);
    document.body.append(tab);
  })`,
  close: `document.getElementById('cap-chat').remove()`,
};

const TAKES = {
  // The screen of the video is one take, from the app with its demo to the end of the install. timeline.json
  // picks its parts by their marks, and plays the slow parts faster.
  'screen': {
    about: 'the screen of the video, in one take: the app, the gallery, VIBE, the AI chat, paste, play, a change, INSTALL',
    async run(t) {
      const prompt = readFileSync(join(HERE, 'night-drive', 'prompt.txt'), 'utf8');
      const answer = readFileSync(join(HERE, 'night-drive', 'answer-1.md'), 'utf8');
      const code = readFileSync(NIGHT_DRIVE, 'utf8');
      const loaded = (name) => t.b.waitFor(`running && wasmName === '${name}' && !compiling`, 180, `${name} runs`);
      const card = (id) => `.card[data-id="${id}"] button`;
      const am = ['KeyA', 'KeyD', 'KeyH'], f = ['KeyA', 'KeyF', 'KeyH'], c = ['KeyA', 'KeyD', 'KeyG'], g = ['KeyS', 'KeyG', 'KeyJ'];

      // Before all: the stand-in FM-1 for the install at the end. The take stops when it is not there: the page
      // must never get the MIDI devices of this computer, where a real FM-1 can be.
      await t.b.eval(readFileSync(join(HERE, 'stand-in-fm1.js'), 'utf8'));
      if (await t.b.eval('navigator.requestMIDIAccess.standIn') !== true) throw new Error('the stand-in FM-1 is not in the page');
      await t.b.eval('navigator.clipboard.writeText = async () => {}');  // COPY works as usual, but the clipboard stays as it is

      // ---- The app: the demo, played with the computer keys ----
      await t.wait(1.0);
      await t.click(PANEL.body, { seconds: 1.0 });  // the page has the keyboard now (not the editor)
      await t.mark('play');
      for (const chord of [c, ['KeyS', 'KeyF', 'KeyH'], ['KeyD', 'KeyG', 'KeyJ'], ['KeyF', 'KeyH', 'KeyK']]) await t.hold(chord, 0.75);
      await t.click(PANEL.key(10), { seconds: 0.7 });  // ARP on
      await t.mark('arp');
      const arp = t.hold(['KeyA', 'KeyD', 'KeyG', 'KeyK'], 4.5);
      await t.wait(0.4);
      await t.turn(PANEL.knob(4), 6, 0.12);  // KNOB 1: the ratio
      await t.turn(PANEL.knob(5), 5, 0.12);  // KNOB 2: brighter
      await arp;
      await t.click(PANEL.key(10), { seconds: 0.6 });  // ARP off
      await t.wait(0.6);

      // ---- The gallery: PROG-1 (the band starts at once), GAME-1 (NEON REEF), AURORA from GitHub ----
      await t.click(card('prog-1'), { seconds: 1.0 });
      await t.mark('band_load');
      await loaded('prog-1');
      await t.mark('band');
      await t.move(PANEL.body, 0.9);
      await t.wait(6.0);
      await t.click(card('game-1'), { seconds: 0.9 });
      await t.mark('game_load');
      await loaded('game-1');
      await t.wait(0.5);
      await t.click(PANEL.key(12), { seconds: 0.8 });  // PLAY: the game starts
      await t.mark('game');
      await t.keyDown('KeyJ');  // J fires; A, D, W and S fly
      for (const [key, seconds] of [['KeyA', 0.7], ['KeyD', 1.1], ['KeyW', 0.5], ['KeyA', 0.8], ['KeyD', 0.9], ['KeyS', 0.4],
        ['KeyA', 1.0], ['KeyD', 1.0]]) {
        await t.hold([key], seconds);
        await t.wait(0.12);
      }
      await t.keyUp('KeyJ');
      await t.wheel('#gallery', 5);  // the card of AURORA is more to the right
      await t.wait(0.4);
      await t.click(card('aurora'), { seconds: 0.8 });
      await t.mark('firmware_load');
      await loaded('aurora');
      await t.mark('firmware');
      await t.wait(0.6);
      await t.click(PANEL.body, { seconds: 0.7 });
      await t.mark('firmware_play');
      for (const chord of [am, f, c, g]) {
        await t.hold(chord, 1.7);
        await t.wait(0.15);
      }
      await t.wait(1.2);

      // ---- VIBE: the dialog opens, the idea is typed, COPY ----
      await t.mark('to_vibe');
      await t.click('#vibe-open', { seconds: 1.3 });
      await t.mark('vibe');
      await t.wait(1.4);
      await t.click('#vibe-clear', { seconds: 0.9 });
      await t.mark('typing');
      await t.type(STORY.idea, 34);
      await t.mark('typed');
      await t.wait(1.0);
      await t.move('#vibe-prompt', 0.9);
      await t.wait(0.8);
      await t.click('#vibe-copy', { seconds: 0.8 });
      await t.mark('copy');
      await t.wait(1.8);
      await t.click('#vibe-close', { seconds: 0.9 });
      await t.wait(0.5);

      // ---- The AI chat, as another tab: the prompt goes in, the answer comes, COPY ----
      await t.b.eval(CHAT.open);
      await t.mark('chat');
      await t.wait(1.0);
      await t.click(CHAT.part('#input'), { seconds: 0.9 });
      await t.b.eval(`cap.keys(['Ctrl', 'V'], 900)`);
      await t.wait(0.25);
      await t.b.eval(`(() => {
        const data = new DataTransfer();
        data.setData('text/plain', ${JSON.stringify(prompt)});
        ${CHAT.frame}.contentDocument.dispatchEvent(new ClipboardEvent('paste', { clipboardData: data, bubbles: true, cancelable: true }));
      })()`);
      await t.mark('prompt');
      await t.wait(1.4);
      await t.click(CHAT.part('#send'), { seconds: 0.7 });
      await t.mark('send');
      await t.wait(1.6);  // the AI "thinks"
      await t.mark('answer');
      // 400 characters a second, the speed of a fast AI chat: the answer takes more than a minute
      t.b.eval(`${CHAT.frame}.contentWindow.chat.answer(${JSON.stringify(answer)}, 400)`).catch(() => {});
      await t.b.waitFor(`${CHAT.frame}.contentWindow.chat.done`, 240, 'the answer is complete');
      await t.mark('answered');
      await t.wait(0.7);
      await t.click(CHAT.part('#copy'), { seconds: 0.7 });
      await t.mark('copy_answer');
      await t.wait(2.0);
      await t.b.eval(CHAT.close);
      await t.mark('back');
      await t.wait(0.9);

      // ---- Ctrl+V on the page: NIGHT DRIVE compiles and runs. Its name, COMPILE, and it is played ----
      await t.move(PANEL.body, 0.8);
      await t.wait(0.4);
      await t.mark('paste');
      await t.paste(code);
      await t.b.waitFor(`document.getElementById('project-name').value === 'no name'`, 10, 'the pasted project');
      await loaded('no name');
      await t.mark('running');
      await t.wait(1.0);
      await t.click('#project-name', { seconds: 0.9 });
      await t.selectAll();
      await t.type('NIGHT DRIVE', 9);
      await t.mark('named');
      await t.click('#compile', { seconds: 1.0 });
      await loaded('NIGHT DRIVE');
      await t.mark('compiled');
      await t.click(PANEL.body, { seconds: 0.8 });  // the keyboard to the page
      await t.mark('perform');
      await STORY.perform(t);
      await t.mark('performed');
      await t.wait(0.8);

      // ---- A change in the code: the sun gets bigger. The editor scrolls to the line, a double click selects the
      // number, the new one is typed, and Ctrl+Enter compiles ----
      await t.wheel('#source', 4, 0.12);
      await t.wait(0.5);
      const number = await t.b.eval(`(() => {
        const area = document.getElementById('source'), style = getComputedStyle(area);
        const lines = area.value.split('\\n'), line = lines.findIndex((text) => text.startsWith('#define SUN_R '));
        const column = lines[line].indexOf('${STORY.edit[0]}', 14);
        const pen = document.createElement('canvas').getContext('2d');
        pen.font = style.fontSize + ' ' + style.fontFamily;  // the font of the editor: each character has this width
        // The page is drawn at a scale: the box is in pixels of the window, the style in pixels of the page.
        const box = area.getBoundingClientRect(), scale = box.width / area.offsetWidth;
        return [box.left + scale * (parseFloat(style.paddingLeft) + (column + 1) * pen.measureText('0').width - area.scrollLeft),
          box.top + scale * (parseFloat(style.paddingTop) + (line + 0.5) * parseFloat(style.lineHeight) - area.scrollTop)];
      })()`);
      await t.mark('edit');
      await t.doubleClick(number, 0.9);
      const selected = await t.b.eval(`(({ value, selectionStart, selectionEnd }) => value.slice(selectionStart, selectionEnd))(document.getElementById('source'))`);
      if (selected !== STORY.edit[0]) throw new Error(`the double click selected ${JSON.stringify(selected)}, not ${STORY.edit[0]}`);
      await t.wait(0.5);
      await t.type(STORY.edit[1], 4);
      await t.mark('edited');
      await t.wait(0.7);
      await t.ctrlEnter([number[0] + 40, number[1] + 70]);
      await t.mark('enter');
      await t.b.waitFor('compiling', 3, 'the compile starts').catch(() => {});
      await loaded('NIGHT DRIVE');
      await t.mark('changed');
      await t.wait(0.5);
      await t.click(PANEL.body, { seconds: 0.8 });
      await t.hold(am, 2.2);
      await t.hold(f, 2.2);
      await t.wait(1.0);

      // ---- INSTALL, on the stand-in FM-1: READ, CONNECT, INSTALL NOW, done ----
      // A control of the dialog can be below the part that shows: the wheel moves the text until it is there.
      const reach = async (selector) => {
        for (let i = 0; i < 40; i++) {
          const below = await t.b.eval(`(() => {
            const box = document.querySelector('${selector}').getBoundingClientRect();
            const view = document.getElementById('install-text').getBoundingClientRect();
            return box.bottom > view.bottom - 12;
          })()`);
          if (!below) return;
          await t.wheel('#install-text', 1, 0.12);
        }
      };
      await t.mark('install');
      await t.click('#install-open', { seconds: 1.0 });
      await t.b.waitFor('installer.package', 120, 'the package is made');
      await t.mark('package');
      await t.wait(1.5);
      await reach('#install-agree');
      await t.wait(0.4);
      await t.click('#install-agree', { seconds: 0.8 });
      await t.mark('agree');
      await reach('#install-next');
      await t.click('#install-next', { seconds: 0.8 });
      await t.mark('connect_step');
      await t.wait(1.2);
      await reach('#install-connect');
      await t.click('#install-connect', { seconds: 0.8 });
      await t.mark('connect');
      await t.b.waitFor('installer.device', 30, 'the stand-in FM-1 is found');
      await t.mark('found');
      await t.wait(1.8);
      await reach('#install-ready');
      await t.click('#install-ready', { seconds: 0.8 });
      await t.mark('write_step');
      await t.wait(1.5);
      await reach('#install-go');
      await t.click('#install-go', { seconds: 0.9 });
      await t.mark('go');
      await t.b.waitFor(`!installer.writing && !document.getElementById('install-done').hidden`, 180, 'the install is done');
      await t.mark('done');
      await t.wait(2.5);
      await t.click('#install-done', { seconds: 0.9 });
      await t.mark('closed');
      await t.wait(1.5);
    },
  },

  'a1-night-drive': {
    about: 'the music: NIGHT DRIVE played by a script, rendered from the program itself (no capture)',
    // The page compiles NIGHT DRIVE as COMPILE does; the program then runs here in Node, as the audio worklet runs
    // it: loop() and then audio() for each block of 256 frames. Each block is exactly in time, and so is the sound.
    async audio(t, ffmpeg) {
      await t.b.eval(`(() => {
        const data = new DataTransfer();
        data.setData('text/plain', ${JSON.stringify(readFileSync(NIGHT_DRIVE, 'utf8'))});
        document.dispatchEvent(new ClipboardEvent('paste', { clipboardData: data, bubbles: true, cancelable: true }));
      })()`);
      await t.b.waitFor(`typeof running !== 'undefined' && running && wasmName === 'no name' && !compiling`, 60, 'NIGHT DRIVE runs');
      const wasm = Buffer.from(await t.b.eval(`(() => { const b = new Uint8Array(wasm); let s = '';
        for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode(...b.subarray(i, i + 0x8000)); return btoa(s); })()`), 'base64');
      const module = await WebAssembly.compile(wasm);
      const imports = {};
      for (const { module: from, name, kind } of WebAssembly.Module.imports(module)) {
        if (kind === 'function') (imports[from] ||= {})[name] = () => 52;  // as the worklet: no system calls
      }
      const program = (await WebAssembly.instantiate(module, imports)).exports;
      program.__wasm_call_ctors();
      program.fm1_sim_master(STORY.master);
      program.setup();
      const events = STORY.music();  // [seconds, function name, ...arguments], in sequence
      const blocks = Math.ceil(STORY.musicSeconds * 44100 / 256);
      const pcm = Buffer.alloc(blocks * 256 * 4);
      let next = 0, peak = 0;
      for (let block = 0; block < blocks; block++) {
        const now = block * 256 / 44100;
        while (next < events.length && events[next][0] <= now) {
          const [, call, ...rest] = events[next++];
          program[call](...rest);
        }
        program.loop();
        const samples = new Int32Array(program.memory.buffer, program.fm1_sim_audio(), 512);
        for (let i = 0; i < 512; i++) {
          const value = Math.max(-32768, Math.min(32767, Math.round(samples[i] / 256)));  // Q23 to 16 bits
          peak = Math.max(peak, Math.abs(value));
          pcm.writeInt16LE(value, (block * 512 + i) * 2);
        }
      }
      const head = Buffer.alloc(44);
      head.write('RIFF', 0); head.writeUInt32LE(36 + pcm.length, 4); head.write('WAVEfmt ', 8);
      head.writeUInt32LE(16, 16); head.writeUInt16LE(1, 20); head.writeUInt16LE(2, 22); head.writeUInt32LE(44100, 24);
      head.writeUInt32LE(44100 * 4, 28); head.writeUInt16LE(4, 32); head.writeUInt16LE(16, 34); head.write('data', 36);
      head.writeUInt32LE(pcm.length, 40);
      const out = join(HERE, 'footage', 'audio', 'a1-night-drive.wav');
      mkdirSync(dirname(out), { recursive: true });
      writeFileSync(out, Buffer.concat([head, pcm]));
      console.log(`  footage/audio/a1-night-drive.wav: ${STORY.musicSeconds} s, peak ${(20 * Math.log10(peak / 32768)).toFixed(1)} dBFS`);
    },
  },
};

// What NIGHT DRIVE is, for the takes: the idea as in STORYBOARD.md, and how a take plays it.
const STORY = {
  idea: 'NIGHT DRIVE: a synthwave arpeggiator. Hold notes and they play as 16th notes over a pulsing bass. PLAY ' +
    'starts and stops a beat (kick, snare, hats). KNOB 1 is the tempo, KNOB 2 the filter, KNOB 3 the echo, ' +
    'KNOB 4 the drive. The screen shows a neon sun over a grid road that moves with the tempo; the held notes ' +
    'are stars in the sky. The LEDs of the held keys pulse on the beat.',
  edit: ['50', '62'],  // the change in the code that the take makes: the radius of the sun, #define SUN_R
  // The playing of NIGHT DRIVE in the take: chords held with the computer keys, PLAY, the knobs. The keys A W S E D F
  // T G Y H U J K are C4 C#4 D4 D#4 E4 F4 F#4 G4 G#4 A4 A#4 B4 C5.
  async perform(t) {
    const am = ['KeyA', 'KeyD', 'KeyH'], f = ['KeyA', 'KeyF', 'KeyH'], c = ['KeyA', 'KeyD', 'KeyG'], g = ['KeyS', 'KeyG', 'KeyJ'];
    await t.hold(am, 2.6);
    await t.click(PANEL.key(12), { seconds: 0.7 });  // PLAY: the beat
    await t.mark('beat');
    for (const chord of [am, f, c, g]) await t.hold(chord, 2.2);
    await t.mark('knobs');
    const holding = t.hold(am, 5.5);
    await t.turn(PANEL.knob(5), -8, 0.1);   // KNOB 2: the filter closes
    await t.turn(PANEL.knob(5), 12, 0.12);  // and opens
    await holding;
    await t.hold(f, 2.2);
    await t.click(PANEL.key(12), { seconds: 0.6 });  // PLAY: the beat stops
  },

  // The music A1: NIGHT DRIVE as events for the program (fm1.h ids: note key n is 14 + n, PLAY is 12; encoders 2
  // to 5 are KNOB 1 to 4). Note key 0 is F3: A3 is 4, C4 7, E4 11, F4 12, G4 14, A4 16, B4 18, C5 19, D5 21.
  // It is written to the bars of timeline.json, where the music starts at the scene "title" (bar 0) and each cut
  // is on a beat of 110 BPM, the tempo at which NIGHT DRIVE starts:
  //   0 title       the arpeggio alone                 19 paste, play, edit   (the simulator sounds; low under edit)
  //   2 app, the gallery   (their own sound)           29 install             the filter opens, then echo and drive
  //   9 vibe, idea  PLAY: the beat comes in            36 payoff              (the real FM-1 sounds)
  //  14.5 ai        the filter closes                  40 end                 the beat stops at 42, the last chord rings
  master: 768,  // the MASTER of the page at its start
  bars: 43,
  get musicSeconds() { return Math.ceil(this.bars * 240 / 110 + 3); },
  music() {
    const events = [];
    const am = [4, 11, 16, 19], chords = [am, [0, 7, 12, 16], [7, 14, 19, 23], [2, 9, 14, 18]];  // Am, F, C, G
    const bar = 240 / 110;
    const hold = (keys, from, to) => keys.forEach((n) => { events.push([from, 'fm1_sim_key', 14 + n, 1], [to, 'fm1_sim_key', 14 + n, 0]); });
    const press = (id, at) => events.push([at, 'fm1_sim_key', id, 1], [at + 0.08, 'fm1_sim_key', id, 0]);
    const turn = (enc, steps, fromBar, bars) => {
      for (let i = 0; i < Math.abs(steps); i++) events.push([(fromBar + bars * i / Math.abs(steps)) * bar, 'fm1_sim_enc', enc, Math.sign(steps)]);
    };
    // One chord a bar. Am in the first two and the last two bars, and where the beat comes in (bar 9).
    for (let n = 0; n < this.bars; n++) {
      const chord = n < 2 || n >= this.bars - 2 ? am : chords[(n - 9 + 400) % 4];
      hold(chord, n * bar, (n + 1) * bar - (n === this.bars - 1 ? 0 : 0.02));
    }
    press(12, 9 * bar);                // PLAY: the beat starts on its first kick, at the cut to "vibe"
    turn(3, -8, 15, 2.5);              // KNOB 2: the filter closes while the AI writes
    turn(3, 10, 29, 4);                // and opens over the first steps of the install
    turn(4, 3, 33, 1.5);               // KNOB 3: more echo
    turn(5, 3, 34.5, 1.5);             // KNOB 4: more drive
    press(12, (this.bars - 1) * bar);  // the beat stops: the last bar is the arpeggio alone
    return events.sort((x, y) => x[0] - y[0]);
  },
};

// ---- Record ----

// The page as on fm1studio.com: the address that the VIBE prompt names is the one of the site, not of the web
// server on this computer.
const AS_ON_THE_SITE = `for (const part of document.querySelectorAll('#vibe [data-fill="site"]')) part.textContent = 'https://fm1studio.com/';`;

async function record(browser, url, name, take, ffmpeg) {
  console.log(`${name}: ${take.about}`);
  const t = new Take(browser, name);
  await browser.goto(url);
  await browser.eval(AS_ON_THE_SITE);
  {
    await browser.waitFor(`document.getElementById('status').textContent === 'Running'`, 120, 'the demo runs');
    await t.wait(1.0);
  }
  if (take.audio) return take.audio(t, ffmpeg);
  await t.tools();
  const raw = join(HERE, 'out', 'capture', `${name}.webm`);
  mkdirSync(dirname(raw), { recursive: true });
  const parts = [];
  const ended = new Promise((done) => {
    const off = browser.on('Runtime.bindingCalled', ({ name: binding, payload }) => {
      if (binding !== 'capOut') return;
      if (payload === 'END') { off(); done(); } else parts.push(Buffer.from(payload, 'base64'));
    });
  });
  await browser.eval('cap.share()');
  await browser.fitWindow();
  const type = await browser.eval('cap.record()');
  const size = await browser.eval(`JSON.stringify({ page: [innerWidth, innerHeight],
    capture: (({ width, height }) => [width, height])(cap.recording.stream.getVideoTracks()[0].getSettings()) })`);
  if (size !== JSON.stringify({ page: [WIDTH, HEIGHT], capture: [WIDTH, HEIGHT] })) console.log(`  note: sizes ${size}`);
  await take.run(t);
  const bytes = await browser.eval('cap.stop()');
  await ended;
  writeFileSync(raw, Buffer.concat(parts));
  console.log(`  recorded ${(bytes / 1048576).toFixed(1)} MB (${type})`);

  // To the footage: 30 frames a second without gaps (the recording only has a frame when the page changed),
  // H.264 at a high quality, the sound as FLAC.
  const out = join(HERE, 'footage', 'screen', `${name}.mkv`);
  mkdirSync(dirname(out), { recursive: true });
  const done = spawnSync(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-y', '-i', raw, '-vf', `fps=${FPS},format=yuv420p`,
    '-c:v', 'libx264', '-preset', 'medium', '-crf', '14', '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709',
    '-c:a', 'flac', '-ar', '48000', out], { encoding: 'utf8' });
  if (done.status) throw new Error(`ffmpeg: ${done.stderr}`);
  writeFileSync(out.replace(/\.mkv$/, '.marks.json'), JSON.stringify(t.marks, null, 2) + '\n');
  console.log(`  footage/screen/${name}.mkv, marks: ${Object.entries(t.marks).map(([k, v]) => `${k} ${v}`).join(', ')}`);
}

async function main() {
  const args = process.argv.slice(2);
  if (args.includes('--list') || !args.length) {
    for (const [name, take] of Object.entries(TAKES)) console.log(`${name.padEnd(10)} ${take.about}`);
    if (!args.length) console.log('\nnode video/capture.mjs TAKE... | --all   [--show] [--url URL]');
    return;
  }
  const urlAt = args.indexOf('--url');
  const names = args.includes('--all') ? Object.keys(TAKES) : args.filter((arg, i) => !arg.startsWith('--') && args[i - 1] !== '--url');
  for (const name of names) if (!TAKES[name]) throw new Error(`no take ${name}: ${Object.keys(TAKES).join(', ')}`);
  if (!existsSync(NIGHT_DRIVE)) throw new Error(`the takes need ${NIGHT_DRIVE}`);
  const ffmpeg = findFfmpeg();
  const server = urlAt >= 0 ? null : await serve();
  const url = urlAt >= 0 ? args[urlAt + 1] : `http://127.0.0.1:${server.address().port}/`;
  const browser = new Browser();
  await browser.start(args.includes('--show'));
  try {
    for (const name of names) await record(browser, url, name, TAKES[name], ffmpeg);
  } finally {
    await browser.stop();
    server?.close();
  }
}

// Started as a script: record. (Imported, it gives its parts, for a test.)
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(`Error: ${error.message}`);
    process.exit(1);
  });
}

export { Browser, Take, serve, findFfmpeg, AS_ON_THE_SITE, NIGHT_DRIVE };

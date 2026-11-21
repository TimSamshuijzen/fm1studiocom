// stand-in-fm1.js - a stand-in for a real FM-1, for the take s5-install of the promotional video.
// capture.mjs runs this script in the page of FM-1 Studio, before the INSTALL dialog opens.
//
// It replaces navigator.requestMIDIAccess with one that gives a single MIDI device, "FM-1", which is this
// script: nothing goes to the MIDI devices of the computer, and no FM-1 is written. The stand-in answers the
// update protocol as an FM-1 does (../src/install/fm1ota.js is the other side):
//   - the identity query: M-VAVE's firmware version 16 at first, the update loader of the package during the
//     install, and the identity of the package after it;
//   - the upgrade command: it reads the parts of the package that a real FM-1 reads (the head and the update
//     loader in step 1; the head, the flash image, and the program area again in step 2), then tells that it
//     is done, and "starts again".
// It keeps nothing of what it reads. Its times are a guess at those of a real FM-1.
(() => {
  const QUERY = 'f0,0,32,45,0,0,0,40,7f,f7', FULL = 'f0,0,32,45,8,0,0,40,0,7e,1,f7', UPGRADE = 'f0,22,24,35,7f,f7';
  const BEFORE = 'FM-1_016';                      // what the FM-1 runs before the install
  const STEP_1 = 0xE0000000, STEP_2 = 0xF0000000;  // the addresses that end the two steps
  const WRITE_SECONDS = 16, RESTART_MS = 2500;

  function pack7(data) {
    const out = [];
    let bits = 0, count = 0;
    for (const byte of data) {
      bits |= byte << count;
      count += 8;
      while (count >= 7) { out.push(bits & 0x7F); bits >>>= 7; count -= 7; }
    }
    if (count) out.push(bits & 0x7F);
    return out;
  }
  function unpack7(data) {
    const out = [];
    let bits = 0, count = 0;
    for (const byte of data) {
      bits |= (byte & 0x7F) << count;
      count += 7;
      while (count >= 8) { out.push(bits & 0xFF); bits >>>= 8; count -= 8; }
    }
    return out;
  }
  // The answer to the identity query: 34 bytes, with the text at byte 6.
  function identity(text) {
    const body = new Array(34).fill(0);
    body[1] = 0x59;
    body[2] = 0x11;
    body[3] = 28;
    [...text].forEach((char, i) => { body[6 + i] = char.charCodeAt(0); });
    return [0xF0, ...pack7(body), 0xF7];
  }
  // A read request: 15 bytes, with the address and the length, and the complement of their sum.
  function request(address, length) {
    const body = [0, 0x59, 0x30, 8, 0, 0, 0, address & 0xFF, (address >>> 8) & 0xFF, (address >>> 16) & 0xFF, (address >>> 24) & 0xFF,
      length & 0xFF, (length >> 8) & 0xFF, 0];
    let sum = 0;
    for (let i = 6; i < 14; i++) sum += body[i];
    body.push(~sum & 0xFF);
    return [0xF0, ...pack7(body), 0xF7];
  }

  const state = { phase: 'firmware', reads: [], waits: '', each: 40, end: 0, product: '' };
  const listeners = new Set();
  const input = {
    id: 'stand-in-in', name: 'FM-1', manufacturer: 'stand-in', type: 'input', state: 'connected', connection: 'open', onmidimessage: null,
    open: async () => input, close: async () => input,
    addEventListener: (type, listener) => { if (type === 'midimessage') listeners.add(listener); },
    removeEventListener: (type, listener) => listeners.delete(listener),
  };
  // The FM-1 "sends" `bytes` after `ms`.
  function send(bytes, ms) {
    setTimeout(() => {
      const event = { data: new Uint8Array(bytes), target: input };
      input.onmidimessage?.(event);
      for (const listener of listeners) listener(event);
    }, ms);
  }
  // The short identity: the name, '_' and three digits. A firmware with a longer one tells it on the second query.
  const short = () => /^([^_]+_\d{3})/.exec(state.product)?.[1] ?? state.product;

  // The reads of one step, in parts of 512 bytes: `ranges` are [from, to) in the package.
  function plan(ranges, end, seconds) {
    state.reads = [];
    for (const [from, to] of ranges) {
      for (let at = from; at < to; at += 512) state.reads.push([at, Math.min(512, to - at)]);
    }
    state.end = end;
    state.each = seconds ? Math.max(5, seconds * 1000 / state.reads.length - 12) : 40;
    next(400);
  }
  function next(ms) {
    const read = state.reads.shift();
    state.waits = read ? 'read' : 'end';  // after the last read, the request with the end address
    send(read ? request(...read) : request(state.end, 8), ms);
  }

  const output = {
    id: 'stand-in-out', name: 'FM-1', manufacturer: 'stand-in', type: 'output', state: 'connected', connection: 'open',
    open: async () => output, close: async () => output,
    send(data) {
      const bytes = Array.from(data), key = bytes.map((byte) => byte.toString(16)).join(',');
      if (state.phase === 'off') return;  // it starts again: no answer
      if (key === QUERY) {
        send(identity(state.phase === 'firmware' ? BEFORE : state.phase === 'loader' ? 'ota-' + short() : short()), 12);
      } else if (key === FULL) {
        if (state.phase === 'new' && state.product !== short()) send(identity(state.product), 12);
      } else if (key === UPGRADE) {
        // `installer` is the page's: the package that the dialog made, and fm1-base's code that knows its parts
        const { image, product } = installer.package, parts = installer.code.ufwEntries(image), flash = parts[0], loader = parts[100];
        state.product = product;
        if (state.phase === 'firmware') {
          plan([[0, 0x400], [0, 0x400], ...(loader ? [[loader.off + 0x20, loader.off + loader.len]] : [])], STEP_1);
        } else if (state.phase === 'loader') {
          plan(flash && flash.len > 0x4000 ? [[0, 0x400], [flash.off, flash.off + 0x20], [flash.off, flash.off + flash.len],
            [flash.off + 0x4000, flash.off + flash.len]] : [[0, image.length]], STEP_2, WRITE_SECONDS);
        }
      } else {
        const body = unpack7(bytes.slice(1, -1));
        if (body[1] !== 0x59 || body[2] !== 0x30) return;  // not the answer to a read
        if (state.waits === 'read') {
          next(state.each);
        } else if (state.waits === 'end') {
          // the answer to the end address: the FM-1 starts again, in the update loader or in the new program
          const to = state.end === STEP_1 ? 'loader' : 'new';
          state.waits = '';
          state.phase = 'off';
          setTimeout(() => { state.phase = to; }, RESTART_MS);
        }
      }
    },
  };

  const access = {
    sysexEnabled: true, inputs: new Map([[input.id, input]]), outputs: new Map([[output.id, output]]), onstatechange: null,
    addEventListener() {}, removeEventListener() {},
  };
  const standIn = async () => access;
  standIn.standIn = true;
  Object.defineProperty(navigator, 'requestMIDIAccess', { configurable: true, value: standIn });
  window.standInFm1 = state;
})();

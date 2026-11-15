# src/install: the installer of fm1-base, for a web page

This directory has the code that writes a `.fwsc` package to an M-VAVE FM-1 from a browser, over USB
MIDI. It is the installer of [fm1-base](https://github.com/charlesvestal/fm1-base), the platform that
the packages of FM-1 Studio are built on. The files are copies, not changed: do not change them by hand.
They are the work of others, under their own licence. Not affiliated with M-VAVE or JieLi.

**No install with this code was done on a real FM-1 by this repository.** In FM-1 Studio it ran only
against a stand-in FM-1 in the page (see "Check the files").

## Contents

| File | Size | What |
| --- | --- | --- |
| `fm1ota.js` | 29 KB | The update protocol: the class `Updater` finds an FM-1, sends a package, and checks the result |
| `fm1ports.js` | 6 KB | Finds the MIDI ports of an FM-1. `fm1ota.js` imports it |
| `fm1pkg.js` | 6 KB | Reads a `.fwsc` package: its identity, the image that the FM-1 reads, its checksums |
| `LICENSE` | 17 KB | The licence text of fm1-base (MPL-2.0) |

## Origins

All files come from fm1-base, commit `c8e05997ce970875038156d2bbd69e712eac1543`: the commit that
`src/fwsc/platform.json` has the platform sources of. A package thus has the update loader that this
installer is made for.

| File | From | Licence |
| --- | --- | --- |
| `fm1ota.js`, `fm1ports.js`, `fm1pkg.js` | `web/` of fm1-base. Copyright Charles Vestal, Leo Kuroshita (Hügelton Instruments) and Chris Farrell: see the head of each file | MPL-2.0 |
| `LICENSE` | The root of fm1-base | |

The update protocol is the one of M-VAVE's own updater (M-UPGRADE). The head of `fm1ota.js` describes it.

## How an install goes

The FM-1 asks, and the computer answers: each message is a MIDI SysEx message.

1. The firmware that runs on the FM-1 gets the upgrade command. It reads the head of the package and
   the update loader that is in the package, and starts that loader.
2. The loader comes back as a MIDI device with a name that starts with `ota-`. It reads the package,
   checks it, and writes the program area of the flash memory. A loader of felucca-bsp can write only
   the parts that changed.
3. The FM-1 starts again. The installer asks its identity, and compares it with the one of the package.

An install that stops in step 2 leaves the FM-1 in update mode. A new install finishes it (`resume`).

## Use the installer

The files are ES modules. A page needs a secure context (`https:` or `localhost`) and a browser with
Web MIDI: Chrome or Edge.

```js
const { Updater } = await import('./install/fm1ota.js');
const { productOf, logicalImage, checkUfw } = await import('./install/fm1pkg.js');

const fwsc = new Uint8Array(await (await fetch('demo.fwsc')).arrayBuffer());
const product = productOf(fwsc);       // the identity, for example "FM-1_58600"
const image = logicalImage(fwsc);      // the package as the FM-1 reads it
checkUfw(image);                       // throws an Error for a damaged package

const access = await navigator.requestMIDIAccess({ sysex: true });  // after a click
const updater = new Updater(access);
const step = (kind, a, bytes, percent) => console.log(kind, a, bytes, percent);

const waiting = await updater.find((id) => /^ota-/i.test(id.model));  // an FM-1 in update mode
if (waiting) {
  waiting.link.close();
  await updater.resume(image, step, product);   // false: the write did not finish
} else {
  await updater.install(image, product, step);  // throws an Error with a `code`
}
```

- `find(filter)` gives `{ link, id, name }` of the first port that answers, or `null`. `id.text` is the
  identity that the FM-1 tells. Close the `link` when you do not use it.
- `findAny(filter)` also asks ports with other names, with a message that only a firmware on fm1-base
  answers.
- The installer asks only ports with a usual name of an FM-1 (`PORT_NAMES` in `fm1ports.js`).
  `allow(name)` adds a port that the user selected. `ports()` gives the names of all ports.
- `step` gets `start`, `verify`, `loader`, `mode`, `write`, `reboot` and `done`.
- The codes of the errors: `notfound`, `model`, `open`, `foreign`, `stopped`, `lost`, `noloader`,
  `noreturn`, `mismatch`, `badreq`. The comments in `fm1ota.js` tell what each one means.

For M-VAVE's own firmware (the way back), check the file first, and give `install` the SHA-256 of the
file as its fourth argument: M-VAVE's Open firmware asks for it.

```js
const { sha256hex, stockChecksum } = await import('./install/fm1pkg.js');
const sha256 = await sha256hex(fwsc);
stockChecksum(product, fwsc.length, sha256);   // throws for version 15 or 16 that is not M-VAVE's own file
await updater.install(image, product, step, sha256);
```

A package of M-VAVE's firmware has the identity `FM-1_0` and two digits. After its install, the FM-1
can tell another identity than the package: the error `mismatch` then means that the install is done.

`fm1ota.js` has more than FM-1 Studio uses: the install of m-bootloader, and the resources of a
firmware.

## Make the files again

```bash
for f in web/fm1ota.js web/fm1ports.js web/fm1pkg.js LICENSE; do curl -L -O https://raw.githubusercontent.com/charlesvestal/fm1-base/c8e05997ce970875038156d2bbd69e712eac1543/$f; done
```

Run this in this directory. For another commit of fm1-base, change `src/fwsc/platform.json` to the same
commit ([firmware/web/README.md](../../firmware/web/README.md)), and put the new hashes in the table
below.

## Check the files

```
f3e001c39f65c34c3624f4dbf927bb2c5e17bc167e1aa7f90039f106f80a77b2  fm1ota.js
8a69f1533928f264526e51157f3606b949d79f6b77f1da5e83e0b31302b89b22  fm1ports.js
908ed3d0560dc9d279e343f189cb219f3017cda9fa8ae10550a5fb66949a8a86  fm1pkg.js
fab3dd6bdab226f1c08630b1dd917e11fcb4ec5e1e020e2c16f83a0a13863e85  LICENSE
```

`sha256sum -c` with this text, in this directory, checks the four files. `.gitattributes` keeps Git from
a change of their line ends.

fm1-base tests this code with a stand-in FM-1 (`tests/installer_*.mjs` in its repository). In FM-1
Studio, the INSTALL dialog was tried in the same way: a stand-in for Web MIDI in the page that answers
as M-VAVE's firmware, then as the update loader, then as the new firmware. That shows that the dialog
and this code work together. It does not show that an install on a real FM-1 works.

## Limits

- **Not tried on an FM-1** by this repository.
- **Chrome and Edge only.** Other browsers have no Web MIDI, or it was not tried.
- **A package without the update loader of felucca-bsp** gets the slow install of M-VAVE's protocol.
  The packages of FM-1 Studio have that loader.

## Used by

The INSTALL button of [FM-1 Studio](https://github.com/TimSamshuijzen/fm1studiocom): the part "Install"
of the page script in `src/index.html`.

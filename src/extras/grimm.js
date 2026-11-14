// Files for GR!MM v2.0.2 in FM-1 Studio that are not in its repository (github.com/GRiMMBIENT/grimm).
//
// fm1studio/grimm_web.c   Not from GR!MM: the functions that the audio worklet of FM-1 Studio calls,
//                         made from the functions of GR!MM's own browser build (firmware/src/fm1_web.c).
//
// GR!MM is GPL-3.0-only. Its samples and other parts have their own licences: see LICENSE, LICENSING.md
// and LICENSES/ of that repository, which FM-1 Studio loads together with the code.
fm1Extras({
'fm1studio/grimm_web.c': `/* SPDX-License-Identifier: GPL-3.0-only */
/* Added for FM-1 Studio, not a file of GR!MM.
 *
 * GR!MM's main loop, app_main() in firmware/src/grimm.c, does not return. GR!MM's own browser build
 * (web/build_web.sh) stops it between two audio blocks with Asyncify, a step of the Binaryen tools.
 * The compiler in FM-1 Studio does not have that step. Thus FM-1 Studio changes app_main() when it
 * loads grimm.c: one call of app_main() is one turn of the main loop. The changes in grimm.c are
 * marked with the words "FM-1 Studio".
 *
 * This file gives the functions that the audio worklet of FM-1 Studio calls. The other web_
 * functions are those of firmware/src/fm1_web.c. */
#include "grimm.c"

/* fm1_web.c calls this when the sound for the worklet is ready. Here app_main() returns then. */
void web_yield(void) {}

/* The clock for GR!MM's CPU meter, 24 ticks in a microsecond. Here it is the time of the device,
 * which does not move during a block: the meter shows 0. */
uint32_t web_ticks(void) { return fm1_ms * 24000u; }

EXPORT void web_boot(void)
{
    memset(fake_flash, 0xFF, sizeof fake_flash);   /* an empty sample area, as web_run() of fm1_web.c makes it */
    app_main();                                    /* the start, and the first turn of the main loop */
}

/* Runs the firmware until it has n frames of sound, then puts them where web_out_l() and
 * web_out_r() point. */
EXPORT void web_render(uint32_t n)
{
    web_want_frames(n);
    while (!web_ready()) app_main();
    web_take();
}

EXPORT uint16_t *web_screen(void) { return fb; }
`,
});

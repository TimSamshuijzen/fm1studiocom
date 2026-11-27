/* main.c - GWAVE for the FM-1: the Defender sound board, emulated.
 *
 * GWAVE (complexity.zone/gwave) plays the sounds of Williams' Defender (1980):
 * the sound ROM by Sam Dicker and Eugene Jarvis, running on a 6802 CPU at
 * 894,886 Hz that makes every sound by writing bytes to an 8-bit DAC.
 *
 * This program does the same in the most direct way: it runs the original
 * program. The ROM below was assembled from the published source VSNDRM1.SRC
 * (github.com/historicalsource/williams-soundroms), and a cycle-counted 6800
 * CPU executes it. Each byte the ROM stores to the DAC is placed at the CPU
 * cycle it was written, then averaged into 44.1 kHz samples and passed through
 * the 25 Hz high-pass of the board's output capacitor (8 ohm speaker, 800 uF).
 * So GWAVE, PULSE, NOISE, LIGHTNING, SCREAM, RADIO, HYPER and the ORGAN all
 * come from the same code the arcade machine ran, with its exact timing.
 *
 * Note keys    SFX mode: play sounds 1 to 27 of the list (see PRESETS)
 *              ORGAN mode: play the ROM's organ, one note at a time
 * SCL          switch the note keys between SFX and ORGAN
 * OCT- OCT+    ORGAN mode: octave (-2 to +1)
 * ALGORITHM    ORGAN mode: organ stops (which octaves sound together)
 * PRESETS      choose a sound command, PLAY plays it
 * SELECT       choose a game sound (a timed series of commands), SEQ plays it
 * HOME         SOUND OFF: stops everything, also the background loops
 * KNOB 1       TONE: a low pass after the DAC (12 is open)
 * MASTER       volume, set by the FM-1 itself: a USB recording gets the full
 *              level
 * MIDI         the same as the note keys (MIDI note 53 is note key 0)
 *
 * The background sounds (15 BACKGROUND, 16 TURBINE STEP) keep running under
 * the other sounds, as in the game, until HOME (or sound 19) stops them.
 */
#include "fm1.h"
#include <math.h>
#include <string.h>

#define BIT(n) (1u << (n))

/* ======================================================================
 * The Defender sound ROM, $F800 to $FFFF (2 KB)
 * ====================================================================== */

static const uint8_t rom[2048] = {
    0xFE, 0x0F, 0x8E, 0x00, 0x7F, 0xCE, 0x04, 0x00, 0x6F, 0x01, 0x6F, 0x03, 0x86, 0xFF, 0xA7, 0x00,  /* $F800 */
    0x6F, 0x02, 0x86, 0x37, 0xA7, 0x03, 0x86, 0x3C, 0xA7, 0x01, 0x97, 0x09, 0x4F, 0x97, 0x07, 0x97,  /* $F810 */
    0x04, 0x97, 0x05, 0x97, 0x06, 0x97, 0x08, 0x0E, 0x20, 0xFE, 0x16, 0x48, 0x48, 0x48, 0x1B, 0xCE,  /* $F820 */
    0x00, 0x13, 0xDF, 0x0F, 0xCE, 0xFD, 0x76, 0xBD, 0xFD, 0x21, 0xC6, 0x09, 0x7E, 0xFB, 0x0A, 0x96,  /* $F830 */
    0x1B, 0xB7, 0x04, 0x00, 0x96, 0x13, 0x97, 0x1C, 0x96, 0x14, 0x97, 0x1D, 0xDE, 0x18, 0x96, 0x1C,  /* $F840 */
    0x73, 0x04, 0x00, 0x09, 0x27, 0x10, 0x4A, 0x26, 0xFA, 0x73, 0x04, 0x00, 0x96, 0x1D, 0x09, 0x27,  /* $F850 */
    0x05, 0x4A, 0x26, 0xFA, 0x20, 0xE8, 0xB6, 0x04, 0x00, 0x2B, 0x01, 0x43, 0x8B, 0x00, 0xB7, 0x04,  /* $F860 */
    0x00, 0x96, 0x1C, 0x9B, 0x15, 0x97, 0x1C, 0x96, 0x1D, 0x9B, 0x16, 0x97, 0x1D, 0x91, 0x17, 0x26,  /* $F870 */
    0xCB, 0x96, 0x1A, 0x27, 0x06, 0x9B, 0x13, 0x97, 0x13, 0x26, 0xB9, 0x39, 0x86, 0x01, 0x97, 0x1A,  /* $F880 */
    0xC6, 0x03, 0x20, 0x0A, 0x86, 0xFE, 0x97, 0x1A, 0x86, 0xC0, 0xC6, 0x10, 0x20, 0x00, 0x97, 0x19,  /* $F890 */
    0x86, 0xFF, 0xB7, 0x04, 0x00, 0xD7, 0x15, 0xD6, 0x15, 0x96, 0x0A, 0x44, 0x44, 0x44, 0x98, 0x0A,  /* $F8A0 */
    0x44, 0x76, 0x00, 0x09, 0x76, 0x00, 0x0A, 0x24, 0x03, 0x73, 0x04, 0x00, 0x96, 0x19, 0x4A, 0x26,  /* $F8B0 */
    0xFD, 0x5A, 0x26, 0xE5, 0x96, 0x19, 0x9B, 0x1A, 0x97, 0x19, 0x26, 0xDB, 0x39, 0x86, 0x20, 0x97,  /* $F8C0 */
    0x15, 0x97, 0x18, 0x86, 0x01, 0xCE, 0x00, 0x01, 0xC6, 0xFF, 0x20, 0x00, 0x97, 0x13, 0xDF, 0x16,  /* $F8D0 */
    0xD7, 0x14, 0xD6, 0x15, 0x96, 0x0A, 0x44, 0x44, 0x44, 0x98, 0x0A, 0x44, 0x76, 0x00, 0x09, 0x76,  /* $F8E0 */
    0x00, 0x0A, 0x86, 0x00, 0x24, 0x02, 0x96, 0x14, 0xB7, 0x04, 0x00, 0xDE, 0x16, 0x09, 0x26, 0xFD,  /* $F8F0 */
    0x5A, 0x26, 0xE1, 0xD6, 0x14, 0xD0, 0x13, 0x27, 0x09, 0xDE, 0x16, 0x08, 0x96, 0x18, 0x27, 0xD0,  /* $F900 */
    0x20, 0xCC, 0x39, 0xC6, 0x01, 0xD7, 0x04, 0x4F, 0x97, 0x19, 0x20, 0x14, 0x4F, 0x97, 0x19, 0xC6,  /* $F910 */
    0x03, 0x20, 0x0D, 0x86, 0x01, 0x97, 0x19, 0xCE, 0x03, 0xE8, 0x86, 0x01, 0xC6, 0xFF, 0x20, 0x00,  /* $F920 */
    0x97, 0x18, 0xD7, 0x13, 0xDF, 0x16, 0x7F, 0x00, 0x15, 0xDE, 0x16, 0xB6, 0x04, 0x00, 0x16, 0x54,  /* $F930 */
    0x54, 0x54, 0xD8, 0x0A, 0x54, 0x76, 0x00, 0x09, 0x76, 0x00, 0x0A, 0xD6, 0x13, 0x7D, 0x00, 0x19,  /* $F940 */
    0x27, 0x02, 0xD4, 0x09, 0xD7, 0x14, 0xD6, 0x15, 0x91, 0x0A, 0x22, 0x12, 0x09, 0x27, 0x26, 0xB7,  /* $F950 */
    0x04, 0x00, 0xDB, 0x15, 0x99, 0x14, 0x25, 0x16, 0x91, 0x0A, 0x23, 0xF0, 0x20, 0x10, 0x09, 0x27,  /* $F960 */
    0x14, 0xB7, 0x04, 0x00, 0xD0, 0x15, 0x92, 0x14, 0x25, 0x04, 0x91, 0x0A, 0x22, 0xF0, 0x96, 0x0A,  /* $F970 */
    0xB7, 0x04, 0x00, 0x20, 0xB9, 0xD6, 0x18, 0x27, 0xB5, 0x96, 0x13, 0xD6, 0x15, 0x44, 0x56, 0x44,  /* $F980 */
    0x56, 0x44, 0x56, 0x43, 0x50, 0x82, 0xFF, 0xDB, 0x15, 0x99, 0x13, 0xD7, 0x15, 0x97, 0x13, 0x26,  /* $F990 */
    0x98, 0xC1, 0x07, 0x26, 0x94, 0x39, 0x86, 0xFD, 0x97, 0x0F, 0xCE, 0x00, 0x64, 0xDF, 0x0B, 0xDB,  /* $F9A0 */
    0x0C, 0x96, 0x11, 0x99, 0x0B, 0x97, 0x11, 0xDE, 0x0B, 0x25, 0x04, 0x20, 0x00, 0x20, 0x03, 0x08,  /* $F9B0 */
    0x27, 0x11, 0xDF, 0x0B, 0x84, 0x0F, 0x8B, 0x9A, 0x97, 0x10, 0xDE, 0x0F, 0xA6, 0x00, 0xB7, 0x04,  /* $F9C0 */
    0x00, 0x20, 0xDC, 0x39, 0x4F, 0xB7, 0x04, 0x00, 0x97, 0x11, 0x4F, 0x91, 0x11, 0x26, 0x03, 0x73,  /* $F9D0 */
    0x04, 0x00, 0xC6, 0x12, 0x5A, 0x26, 0xFD, 0x4C, 0x2A, 0xF1, 0x73, 0x04, 0x00, 0x7C, 0x00, 0x11,  /* $F9E0 */
    0x2A, 0xE8, 0x39, 0xCE, 0x00, 0x13, 0x6F, 0x00, 0x08, 0x8C, 0x00, 0x1B, 0x26, 0xF8, 0x86, 0x40,  /* $F9F0 */
    0x97, 0x13, 0xCE, 0x00, 0x13, 0x86, 0x80, 0x97, 0x11, 0x5F, 0xA6, 0x01, 0xAB, 0x00, 0xA7, 0x01,  /* $FA00 */
    0x2A, 0x02, 0xDB, 0x11, 0x74, 0x00, 0x11, 0x08, 0x08, 0x8C, 0x00, 0x1B, 0x26, 0xEC, 0xF7, 0x04,  /* $FA10 */
    0x00, 0x7C, 0x00, 0x12, 0x26, 0xDC, 0xCE, 0x00, 0x13, 0x5F, 0xA6, 0x00, 0x27, 0x0B, 0x81, 0x37,  /* $FA20 */
    0x26, 0x04, 0xC6, 0x41, 0xE7, 0x02, 0x6A, 0x00, 0x5C, 0x08, 0x08, 0x8C, 0x00, 0x1B, 0x26, 0xEA,  /* $FA30 */
    0x5D, 0x26, 0xBF, 0x39, 0x7A, 0x00, 0x08, 0x39, 0x7F, 0x00, 0x08, 0x97, 0x11, 0xCE, 0xFD, 0xAA,  /* $FA40 */
    0xA6, 0x00, 0x27, 0x2D, 0x7A, 0x00, 0x11, 0x27, 0x06, 0x4C, 0xBD, 0xFD, 0x21, 0x20, 0xF1, 0x08,  /* $FA50 */
    0xDF, 0x0F, 0xBD, 0xFD, 0x21, 0xDF, 0x0D, 0xDE, 0x0F, 0xA6, 0x00, 0x97, 0x15, 0xA6, 0x01, 0xEE,  /* $FA60 */
    0x02, 0xDF, 0x13, 0x8D, 0x3E, 0xDE, 0x0F, 0x08, 0x08, 0x08, 0x08, 0xDF, 0x0F, 0x9C, 0x0D, 0x26,  /* $FA70 */
    0xE8, 0x7E, 0xFD, 0x0E, 0x86, 0x03, 0x97, 0x08, 0x39, 0x7A, 0x00, 0x08, 0x27, 0x0C, 0xD6, 0x15,  /* $FA80 */
    0x58, 0x58, 0x58, 0x58, 0x1B, 0x97, 0x15, 0x4F, 0x20, 0xFE, 0x4A, 0x81, 0x0B, 0x23, 0x01, 0x4F,  /* $FA90 */
    0xCE, 0xFE, 0x41, 0xBD, 0xFD, 0x21, 0xA6, 0x00, 0xCE, 0xFF, 0xFF, 0xDF, 0x13, 0x8D, 0x04, 0x8D,  /* $FAA0 */
    0x2A, 0x20, 0xFC, 0xCE, 0x00, 0x16, 0x81, 0x00, 0x27, 0x15, 0x81, 0x03, 0x27, 0x09, 0xC6, 0x01,  /* $FAB0 */
    0xE7, 0x00, 0x08, 0x80, 0x02, 0x20, 0xEF, 0xC6, 0x91, 0xE7, 0x00, 0x6F, 0x01, 0x08, 0x08, 0xC6,  /* $FAC0 */
    0x7E, 0xE7, 0x00, 0xC6, 0xFA, 0xE7, 0x01, 0xC6, 0xDD, 0xE7, 0x02, 0xDE, 0x13, 0x4F, 0xF6, 0x00,  /* $FAD0 */
    0x12, 0x5C, 0xD7, 0x12, 0xD4, 0x15, 0x54, 0x89, 0x00, 0x54, 0x89, 0x00, 0x54, 0x89, 0x00, 0x54,  /* $FAE0 */
    0x89, 0x00, 0x54, 0x89, 0x00, 0x54, 0x89, 0x00, 0x54, 0x89, 0x00, 0x1B, 0x48, 0x48, 0x48, 0x48,  /* $FAF0 */
    0xB7, 0x04, 0x00, 0x09, 0x27, 0x03, 0x7E, 0x00, 0x16, 0x39, 0x36, 0xA6, 0x00, 0xDF, 0x0D, 0xDE,  /* $FB00 */
    0x0F, 0xA7, 0x00, 0x08, 0xDF, 0x0F, 0xDE, 0x0D, 0x08, 0x5A, 0x26, 0xEF, 0x32, 0x39, 0x4F, 0x97,  /* $FB10 */
    0x04, 0x97, 0x05, 0x39, 0x7F, 0x00, 0x04, 0x96, 0x05, 0x84, 0x7F, 0x81, 0x1D, 0x26, 0x01, 0x4F,  /* $FB20 */
    0x4C, 0x97, 0x05, 0x39, 0x86, 0x0E, 0xBD, 0xFB, 0x81, 0x96, 0x05, 0x48, 0x48, 0x43, 0xBD, 0xFC,  /* $FB30 */
    0x39, 0x7C, 0x00, 0x17, 0xBD, 0xFC, 0x3B, 0x20, 0xF8, 0x86, 0x03, 0xBD, 0xF8, 0x2A, 0xD6, 0x06,  /* $FB40 */
    0xC1, 0x1F, 0x26, 0x01, 0x5F, 0x5C, 0xD7, 0x06, 0x86, 0x20, 0x10, 0x5F, 0x81, 0x14, 0x23, 0x05,  /* $FB50 */
    0xCB, 0x0E, 0x4A, 0x20, 0xF7, 0xCB, 0x05, 0x4A, 0x26, 0xFB, 0xD7, 0x13, 0xBD, 0xF8, 0x3F, 0x20,  /* $FB60 */
    0xFB, 0x96, 0x07, 0x26, 0x09, 0x7C, 0x00, 0x07, 0x86, 0x0D, 0x8D, 0x05, 0x20, 0x69, 0x7E, 0xFC,  /* $FB70 */
    0x2E, 0x16, 0x58, 0x1B, 0x1B, 0x1B, 0xCE, 0xFE, 0xEC, 0xBD, 0xFD, 0x21, 0xA6, 0x00, 0x16, 0x84,  /* $FB80 */
    0x0F, 0x97, 0x14, 0x54, 0x54, 0x54, 0x54, 0xD7, 0x13, 0xA6, 0x01, 0x16, 0x54, 0x54, 0x54, 0x54,  /* $FB90 */
    0xD7, 0x15, 0x84, 0x0F, 0x97, 0x11, 0xDF, 0x0B, 0xCE, 0xFE, 0x4D, 0x7A, 0x00, 0x11, 0x2B, 0x08,  /* $FBA0 */
    0xA6, 0x00, 0x4C, 0xBD, 0xFD, 0x21, 0x20, 0xF3, 0xDF, 0x18, 0xBD, 0xFC, 0x75, 0xDE, 0x0B, 0xA6,  /* $FBB0 */
    0x02, 0x97, 0x1A, 0xBD, 0xFC, 0x87, 0xDE, 0x0B, 0xA6, 0x03, 0x97, 0x16, 0xA6, 0x04, 0x97, 0x17,  /* $FBC0 */
    0xA6, 0x05, 0x16, 0xA6, 0x06, 0xCE, 0xFF, 0x55, 0xBD, 0xFD, 0x21, 0x17, 0xDF, 0x1B, 0x7F, 0x00,  /* $FBD0 */
    0x23, 0xBD, 0xFD, 0x21, 0xDF, 0x1D, 0x39, 0x96, 0x13, 0x97, 0x22, 0xDE, 0x1B, 0xDF, 0x0D, 0xDE,  /* $FBE0 */
    0x0D, 0xA6, 0x00, 0x9B, 0x23, 0x97, 0x21, 0x9C, 0x1D, 0x27, 0x26, 0xD6, 0x14, 0x08, 0xDF, 0x0D,  /* $FBF0 */
    0xCE, 0x00, 0x24, 0x96, 0x21, 0x4A, 0x26, 0xFD, 0xA6, 0x00, 0xB7, 0x04, 0x00, 0x08, 0x9C, 0x1F,  /* $FC00 */
    0x26, 0xF1, 0x5A, 0x27, 0xDA, 0x08, 0x09, 0x08, 0x09, 0x08, 0x09, 0x08, 0x09, 0x01, 0x01, 0x20,  /* $FC10 */
    0xDF, 0x96, 0x15, 0x8D, 0x62, 0x7A, 0x00, 0x22, 0x26, 0xC1, 0x96, 0x07, 0x26, 0x46, 0x96, 0x16,  /* $FC20 */
    0x27, 0x42, 0x7A, 0x00, 0x17, 0x27, 0x3D, 0x9B, 0x23, 0x97, 0x23, 0xDE, 0x1B, 0x5F, 0x96, 0x23,  /* $FC30 */
    0x7D, 0x00, 0x16, 0x2B, 0x06, 0xAB, 0x00, 0x25, 0x08, 0x20, 0x0B, 0xAB, 0x00, 0x27, 0x02, 0x25,  /* $FC40 */
    0x05, 0x5D, 0x27, 0x08, 0x20, 0x0F, 0x5D, 0x26, 0x03, 0xDF, 0x1B, 0x5C, 0x08, 0x9C, 0x1D, 0x26,  /* $FC50 */
    0xDD, 0x5D, 0x26, 0x01, 0x39, 0xDF, 0x1D, 0x96, 0x15, 0x27, 0x06, 0x8D, 0x08, 0x96, 0x1A, 0x8D,  /* $FC60 */
    0x16, 0x7E, 0xFB, 0xE7, 0x39, 0xCE, 0x00, 0x24, 0xDF, 0x0F, 0xDE, 0x18, 0xE6, 0x00, 0x08, 0xBD,  /* $FC70 */
    0xFB, 0x0A, 0xDE, 0x0F, 0xDF, 0x1F, 0x39, 0x4D, 0x27, 0x2B, 0xDE, 0x18, 0xDF, 0x0D, 0xCE, 0x00,  /* $FC80 */
    0x24, 0x97, 0x12, 0xDF, 0x0F, 0xDE, 0x0D, 0xD6, 0x12, 0xD7, 0x11, 0xE6, 0x01, 0x54, 0x54, 0x54,  /* $FC90 */
    0x54, 0x08, 0xDF, 0x0D, 0xDE, 0x0F, 0xA6, 0x00, 0x10, 0x7A, 0x00, 0x11, 0x26, 0xFA, 0xA7, 0x00,  /* $FCA0 */
    0x08, 0x9C, 0x1F, 0x26, 0xDE, 0x39, 0x8E, 0x00, 0x7F, 0xB6, 0x04, 0x02, 0x0E, 0x43, 0x84, 0x1F,  /* $FCB0 */
    0xD6, 0x08, 0x27, 0x09, 0x2A, 0x03, 0xBD, 0xFA, 0x48, 0x4A, 0xBD, 0xFA, 0x89, 0x5F, 0x81, 0x0E,  /* $FCC0 */
    0x27, 0x02, 0xD7, 0x06, 0x81, 0x12, 0x27, 0x02, 0xD7, 0x07, 0xF6, 0xEF, 0xFD, 0xC1, 0x7E, 0x26,  /* $FCD0 */
    0x03, 0xBD, 0xEF, 0xFD, 0x4D, 0x27, 0x27, 0x4A, 0x81, 0x0C, 0x22, 0x08, 0xBD, 0xFB, 0x81, 0xBD,  /* $FCE0 */
    0xFB, 0xE7, 0x20, 0x1A, 0x81, 0x1B, 0x22, 0x0E, 0x80, 0x0D, 0x48, 0xCE, 0xFD, 0x58, 0x8D, 0x21,  /* $FCF0 */
    0xEE, 0x00, 0xAD, 0x00, 0x20, 0x08, 0x80, 0x1C, 0xBD, 0xF8, 0x2A, 0xBD, 0xF8, 0x3F, 0x96, 0x04,  /* $FD00 */
    0x9A, 0x05, 0x27, 0xFE, 0x4F, 0x97, 0x07, 0x96, 0x04, 0x27, 0x03, 0x7E, 0xF9, 0x13, 0x7E, 0xFB,  /* $FD10 */
    0x34, 0xDF, 0x0D, 0x9B, 0x0E, 0x97, 0x0E, 0x24, 0x03, 0x7C, 0x00, 0x0D, 0xDE, 0x0D, 0x39, 0x0F,  /* $FD20 */
    0x8E, 0x00, 0x7F, 0xCE, 0xFF, 0xFF, 0x5F, 0xE9, 0x00, 0x09, 0x8C, 0xF8, 0x00, 0x26, 0xF8, 0xE1,  /* $FD30 */
    0x00, 0x27, 0x01, 0x3E, 0x86, 0x01, 0xBD, 0xF8, 0x2A, 0xBD, 0xF8, 0x3F, 0xF6, 0xEF, 0xFA, 0xC1,  /* $FD40 */
    0x7E, 0x26, 0xDC, 0xBD, 0xEF, 0xFA, 0x20, 0xD7, 0xFB, 0x49, 0xF9, 0x13, 0xFB, 0x24, 0xF8, 0x8C,  /* $FD50 */
    0xFB, 0x71, 0xFB, 0x1E, 0xF8, 0xCD, 0xF8, 0x94, 0xF9, 0x1C, 0xF9, 0x23, 0xF9, 0xA6, 0xF9, 0xD4,  /* $FD60 */
    0xF9, 0xF3, 0xFA, 0x44, 0xFA, 0x84, 0x40, 0x01, 0x00, 0x10, 0xE1, 0x00, 0x80, 0xFF, 0xFF, 0x28,  /* $FD70 */
    0x01, 0x00, 0x08, 0x81, 0x02, 0x00, 0xFF, 0xFF, 0x28, 0x81, 0x00, 0xFC, 0x01, 0x02, 0x00, 0xFC,  /* $FD80 */
    0xFF, 0xFF, 0x01, 0x00, 0x18, 0x41, 0x04, 0x80, 0x00, 0xFF, 0x8C, 0x5B, 0xB6, 0x40, 0xBF, 0x49,  /* $FD90 */
    0xA4, 0x73, 0x73, 0xA4, 0x49, 0xBF, 0x40, 0xB6, 0x5B, 0x8C, 0x0C, 0x7F, 0x1D, 0x0F, 0xFB, 0x7F,  /* $FDA0 */
    0x23, 0x0F, 0x15, 0xFE, 0x08, 0x50, 0x8A, 0x88, 0x3E, 0x3F, 0x02, 0x3E, 0x7C, 0x04, 0x03, 0xFF,  /* $FDB0 */
    0x3E, 0x3F, 0x2C, 0xE2, 0x7C, 0x12, 0x0D, 0x74, 0x7C, 0x0D, 0x0E, 0x41, 0x7C, 0x23, 0x0B, 0x50,  /* $FDC0 */
    0x7C, 0x1D, 0x29, 0xF2, 0x7C, 0x3F, 0x02, 0x3E, 0xF8, 0x04, 0x03, 0xFF, 0x7C, 0x3F, 0x2C, 0xE2,  /* $FDD0 */
    0xF8, 0x12, 0x0D, 0x74, 0xF8, 0x0D, 0x0E, 0x41, 0xF8, 0x23, 0x0B, 0x50, 0xF8, 0x1D, 0x2F, 0xF2,  /* $FDE0 */
    0xF8, 0x23, 0x05, 0xA8, 0xF8, 0x12, 0x06, 0xBA, 0xF8, 0x04, 0x07, 0xFF, 0x7C, 0x37, 0x04, 0xC1,  /* $FDF0 */
    0x7C, 0x23, 0x05, 0xA8, 0x7C, 0x12, 0x06, 0xBA, 0x3E, 0x04, 0x07, 0xFF, 0x3E, 0x37, 0x04, 0xC1,  /* $FE00 */
    0x3E, 0x23, 0x05, 0xA8, 0x1F, 0x12, 0x06, 0xBA, 0x1F, 0x04, 0x07, 0xFF, 0x1F, 0x37, 0x04, 0xC1,  /* $FE10 */
    0x1F, 0x23, 0x16, 0xA0, 0xFE, 0x1D, 0x17, 0xF9, 0x7F, 0x37, 0x13, 0x06, 0x7F, 0x3F, 0x08, 0xFA,  /* $FE20 */
    0xFE, 0x04, 0x0F, 0xFF, 0xFE, 0x0D, 0x0E, 0x41, 0xFE, 0x23, 0x0B, 0x50, 0xFE, 0x1D, 0x5F, 0xE4,  /* $FE30 */
    0x00, 0x47, 0x3F, 0x37, 0x30, 0x29, 0x23, 0x1D, 0x17, 0x12, 0x0D, 0x08, 0x04, 0x08, 0x7F, 0xD9,  /* $FE40 */
    0xFF, 0xD9, 0x7F, 0x24, 0x00, 0x24, 0x08, 0x00, 0x40, 0x80, 0x00, 0xFF, 0x00, 0x80, 0x40, 0x10,  /* $FE50 */
    0x7F, 0xB0, 0xD9, 0xF5, 0xFF, 0xF5, 0xD9, 0xB0, 0x7F, 0x4E, 0x24, 0x09, 0x00, 0x09, 0x24, 0x4E,  /* $FE60 */
    0x10, 0x7F, 0xC5, 0xEC, 0xE7, 0xBF, 0x8D, 0x6D, 0x6A, 0x7F, 0x94, 0x92, 0x71, 0x40, 0x17, 0x12,  /* $FE70 */
    0x39, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,  /* $FE80 */
    0x00, 0x00, 0x48, 0x8A, 0x95, 0xA0, 0xAB, 0xB5, 0xBF, 0xC8, 0xD1, 0xDA, 0xE1, 0xE8, 0xEE, 0xF3,  /* $FE90 */
    0xF7, 0xFB, 0xFD, 0xFE, 0xFF, 0xFE, 0xFD, 0xFB, 0xF7, 0xF3, 0xEE, 0xE8, 0xE1, 0xDA, 0xD1, 0xC8,  /* $FEA0 */
    0xBF, 0xB5, 0xAB, 0xA0, 0x95, 0x8A, 0x7F, 0x75, 0x6A, 0x5F, 0x54, 0x4A, 0x40, 0x37, 0x2E, 0x25,  /* $FEB0 */
    0x1E, 0x17, 0x11, 0x0C, 0x08, 0x04, 0x02, 0x01, 0x00, 0x01, 0x02, 0x04, 0x08, 0x0C, 0x11, 0x17,  /* $FEC0 */
    0x1E, 0x25, 0x2E, 0x37, 0x40, 0x4A, 0x54, 0x5F, 0x6A, 0x75, 0x7F, 0x10, 0x59, 0x7B, 0x98, 0xAC,  /* $FED0 */
    0xB3, 0xAC, 0x98, 0x7B, 0x59, 0x37, 0x19, 0x06, 0x00, 0x06, 0x19, 0x37, 0x81, 0x24, 0x00, 0x00,  /* $FEE0 */
    0x00, 0x16, 0x31, 0x12, 0x05, 0x1A, 0xFF, 0x00, 0x27, 0x6D, 0x11, 0x05, 0x11, 0x01, 0x0F, 0x01,  /* $FEF0 */
    0x47, 0x11, 0x31, 0x00, 0x01, 0x00, 0x0D, 0x1B, 0xF4, 0x12, 0x00, 0x00, 0x00, 0x14, 0x47, 0x41,  /* $FF00 */
    0x45, 0x00, 0x00, 0x00, 0x0F, 0x5B, 0x21, 0x35, 0x11, 0xFF, 0x00, 0x0D, 0x1B, 0x15, 0x00, 0x00,  /* $FF10 */
    0xFD, 0x00, 0x01, 0x69, 0x31, 0x11, 0x00, 0x01, 0x00, 0x03, 0x6A, 0x01, 0x15, 0x01, 0x01, 0x01,  /* $FF20 */
    0x01, 0x47, 0xF6, 0x53, 0x03, 0x00, 0x02, 0x06, 0x94, 0x6A, 0x10, 0x02, 0x00, 0x02, 0x06, 0x9A,  /* $FF30 */
    0x1F, 0x12, 0x00, 0xFF, 0x10, 0x04, 0x69, 0x31, 0x11, 0x00, 0xFF, 0x00, 0x0D, 0x00, 0x12, 0x06,  /* $FF40 */
    0x00, 0xFF, 0x01, 0x09, 0x28, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x78, 0x70, 0x68, 0x60, 0x58, 0x50,  /* $FF50 */
    0x44, 0x40, 0x01, 0x01, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x30, 0x60, 0xC0, 0xE0,  /* $FF60 */
    0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x80, 0x7C, 0x78,  /* $FF70 */
    0x74, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x01, 0x01, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x20,  /* $FF80 */
    0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x60, 0x70, 0x80, 0xA0, 0xB0, 0xC0, 0x08, 0x40, 0x08, 0x40,  /* $FF90 */
    0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x40,  /* $FFA0 */
    0x01, 0x02, 0x04, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0E, 0x0F, 0x10, 0x12, 0x14, 0x16, 0x40, 0x10,  /* $FFB0 */
    0x08, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x06, 0x08, 0x0A,  /* $FFC0 */
    0x0C, 0x10, 0x14, 0x18, 0x20, 0x30, 0x40, 0x50, 0x40, 0x30, 0x20, 0x10, 0x0C, 0x0A, 0x08, 0x07,  /* $FFD0 */
    0x06, 0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x01, 0x01, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x08, 0x17,  /* $FFE0 */
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x00, 0x00, 0x00, 0xFC, 0xB6, 0xF8, 0x01, 0xFD, 0x2F, 0xF8, 0x01,  /* $FFF0 */
};

/* ======================================================================
 * The sound board: 6802 CPU, 128 bytes of RAM, a 6821 PIA with the DAC
 * ====================================================================== */

#define CPU_CLOCK (3579545.0f / 4.0f) /* 894,886 Hz */
#define FRAME_CYCLES (CPU_CLOCK / 60.0f) /* one game frame */

enum { FC = 1, FV = 2, FZ = 4, FN = 8, FI = 16, FH = 32 };

static struct {
    uint8_t a, b, cc;
    uint16_t x, sp, pc;
    int waiting; /* after WAI */
} cpu;

static uint8_t ram[128];
static uint8_t dac = 0x80; /* port A of the PIA: the DAC */
static uint8_t ddra, ddrb, cra, crb;
static uint8_t port_b = 0xFF; /* the command lines (active low) */
static int irq_flag; /* CB1 interrupt: a command is waiting */

static uint8_t rd(uint16_t ad) {
    if (ad >= 0xF800) return rom[ad - 0xF800];
    if (ad < 0x0400) return ram[ad & 0x7F];
    if (ad < 0x0800) {
        switch (ad & 3) {
        case 0: return (cra & 4) ? dac : ddra;
        case 1: return cra;
        case 2:
            if (!(crb & 4)) return ddrb;
            irq_flag = 0; /* reading port B clears the interrupt */
            return port_b;
        default: return (uint8_t)(crb | (irq_flag ? 0x80 : 0));
        }
    }
    return 0xFF; /* no speech ROM at $EFFD: the ROM skips the talking */
}

static void wr(uint16_t ad, uint8_t v) {
    if (ad < 0x0400) { ram[ad & 0x7F] = v; return; }
    if (ad < 0x0800) {
        switch (ad & 3) {
        case 0: if (cra & 4) dac = v; else ddra = v; break;
        case 1: cra = v & 0x3F; break;
        case 2: if (!(crb & 4)) ddrb = v; break;
        default: crb = v & 0x3F; break;
        }
    }
}

static uint16_t rd16(uint16_t ad) { return (uint16_t)(rd(ad) << 8 | rd((uint16_t)(ad + 1))); }
static uint8_t fetch(void) { return rd(cpu.pc++); }
static uint16_t fetch16(void) { uint16_t v = rd16(cpu.pc); cpu.pc += 2; return v; }
static void push(uint8_t v) { wr(cpu.sp--, v); }
static uint8_t pull(void) { return rd(++cpu.sp); }

static void set(int flag, int on) { if (on) cpu.cc |= flag; else cpu.cc &= ~flag; }
static void nz8(uint8_t r) { set(FN, r & 0x80); set(FZ, r == 0); }
static void nz16(uint16_t r) { set(FN, r & 0x8000); set(FZ, r == 0); }

static uint8_t add8(uint8_t a, uint8_t m, int carry) {
    unsigned r = a + m + carry;
    set(FH, (a ^ m ^ r) & 0x10);
    set(FV, (a ^ r) & (m ^ r) & 0x80);
    set(FC, r & 0x100);
    nz8((uint8_t)r);
    return (uint8_t)r;
}

static uint8_t sub8(uint8_t a, uint8_t m, int carry) {
    unsigned r = (unsigned)a - m - carry;
    set(FV, (a ^ m) & (a ^ r) & 0x80);
    set(FC, r & 0x100);
    nz8((uint8_t)r);
    return (uint8_t)r;
}

/* The read-modify-write instructions: NEG, COM, LSR, ROR, ASR, ASL, ROL, DEC, INC, TST, CLR */
static uint8_t unary(int op, uint8_t m) {
    uint8_t r = m;
    int c = cpu.cc & FC;
    switch (op) {
    case 0x0: r = (uint8_t)(0 - m); set(FC, r != 0); set(FV, r == 0x80); break;
    case 0x3: r = (uint8_t)~m; set(FC, 1); set(FV, 0); break;
    case 0x4: r = m >> 1; set(FC, m & 1); break;
    case 0x6: r = (uint8_t)(m >> 1 | c << 7); set(FC, m & 1); break;
    case 0x7: r = (uint8_t)(m >> 1 | (m & 0x80)); set(FC, m & 1); break;
    case 0x8: r = (uint8_t)(m << 1); set(FC, m & 0x80); break;
    case 0x9: r = (uint8_t)(m << 1 | c); set(FC, m & 0x80); break;
    case 0xA: r = m - 1; set(FV, m == 0x80); break;
    case 0xC: r = m + 1; set(FV, m == 0x7F); break;
    case 0xD: set(FV, 0); set(FC, 0); break;
    case 0xF: r = 0; set(FV, 0); set(FC, 0); break;
    default: return m; /* not an instruction */
    }
    nz8(r);
    if (op == 0x4 || op == 0x6 || op == 0x7 || op == 0x8 || op == 0x9)
        set(FV, !(cpu.cc & FN) != !(cpu.cc & FC)); /* V = N xor C after shifts */
    return r;
}

static int branch_taken(int op) {
    int c = cpu.cc & FC, v = !!(cpu.cc & FV), z = cpu.cc & FZ, n = !!(cpu.cc & FN);
    switch (op & 0xF) {
    case 0x0: return 1;
    case 0x2: return !(c || z);
    case 0x3: return c || z;
    case 0x4: return !c;
    case 0x5: return c;
    case 0x6: return !z;
    case 0x7: return z;
    case 0x8: return !v;
    case 0x9: return v;
    case 0xA: return !n;
    case 0xB: return n;
    case 0xC: return n == v;
    case 0xD: return n != v;
    case 0xE: return !z && n == v;
    default: return z || n != v;
    }
}

static void push_all(void) {
    push((uint8_t)cpu.pc); push((uint8_t)(cpu.pc >> 8));
    push((uint8_t)cpu.x); push((uint8_t)(cpu.x >> 8));
    push(cpu.a); push(cpu.b); push(cpu.cc);
}

static void cpu_reset(void) {
    memset(&cpu, 0, sizeof cpu);
    cpu.cc = 0xC0 | FI;
    cpu.pc = rd16(0xFFFE);
}

/* Runs one instruction (or takes the interrupt) and returns its CPU cycles. */
static int cpu_step(void) {
    if (irq_flag && (crb & 1) && !(cpu.cc & FI)) {
        if (!cpu.waiting) push_all();
        cpu.waiting = 0;
        cpu.cc |= FI;
        cpu.pc = rd16(0xFFF8);
        return 12;
    }
    if (cpu.waiting) return 4;

    uint8_t op = fetch();
    int hi = op >> 4, lo = op & 0xF;

    /* 0x40-0x7F: NEG COM LSR ROR ASR ASL ROL DEC INC TST JMP CLR on A, B, indexed or extended */
    if (hi >= 4 && hi <= 7) {
        if (hi == 4) { cpu.a = unary(lo, cpu.a); return 2; }
        if (hi == 5) { cpu.b = unary(lo, cpu.b); return 2; }
        uint16_t ad = hi == 6 ? (uint16_t)(cpu.x + fetch()) : fetch16();
        if (lo == 0xE) { cpu.pc = ad; return hi == 6 ? 4 : 3; } /* JMP */
        uint8_t r = unary(lo, rd(ad));
        if (lo != 0xD) wr(ad, r); /* TST only reads */
        return hi == 6 ? 7 : 6;
    }

    /* 0x80-0xFF: the accumulator instructions and the 16-bit loads and stores */
    if (hi >= 8) {
        int mode = hi & 3; /* 0 immediate, 1 direct, 2 indexed, 3 extended */
        int is_b = hi >= 0xC;
        uint8_t *acc = is_b ? &cpu.b : &cpu.a;
        int wide = lo == 0xC || lo == 0xE || lo == 0xF;
        uint16_t ad = 0;

        if (lo == 0xD) { /* BSR, JSR */
            if (is_b) return 2; /* not an instruction */
            if (mode == 0) {
                int8_t off = (int8_t)fetch();
                push((uint8_t)cpu.pc); push((uint8_t)(cpu.pc >> 8));
                cpu.pc = (uint16_t)(cpu.pc + off);
                return 8;
            }
            if (mode == 1) return 2;
            ad = mode == 2 ? (uint16_t)(cpu.x + fetch()) : fetch16();
            push((uint8_t)cpu.pc); push((uint8_t)(cpu.pc >> 8));
            cpu.pc = ad;
            return mode == 2 ? 8 : 9;
        }

        static const uint8_t base_cycles[4] = { 2, 3, 5, 4 };
        int cycles = base_cycles[mode] + (wide ? 1 : 0);
        if (mode == 0) ad = cpu.pc, cpu.pc += wide ? 2 : 1;
        else if (mode == 1) ad = fetch();
        else if (mode == 2) ad = (uint16_t)(cpu.x + fetch());
        else ad = fetch16();

        if (lo == 0x7 || lo == 0xF) { /* stores */
            if (mode == 0) return 2;
            if (lo == 0x7) {
                wr(ad, *acc); nz8(*acc); set(FV, 0);
                return cycles + 1;
            }
            uint16_t v = is_b ? cpu.x : cpu.sp; /* STX, STS */
            wr(ad, (uint8_t)(v >> 8)); wr((uint16_t)(ad + 1), (uint8_t)v);
            nz16(v); set(FV, 0);
            return cycles + 1;
        }
        if (wide) {
            uint16_t m = rd16(ad);
            if (lo == 0xC) { /* CPX */
                if (is_b) return 2;
                uint16_t r = (uint16_t)(cpu.x - m);
                nz16(r);
                set(FV, (cpu.x ^ m) & (cpu.x ^ r) & 0x8000);
            } else { /* LDX, LDS */
                if (is_b) cpu.x = m; else cpu.sp = m;
                nz16(m); set(FV, 0);
            }
            return cycles;
        }
        uint8_t m = rd(ad);
        switch (lo) {
        case 0x0: *acc = sub8(*acc, m, 0); break;                /* SUB */
        case 0x1: sub8(*acc, m, 0); break;                       /* CMP */
        case 0x2: *acc = sub8(*acc, m, cpu.cc & FC); break;      /* SBC */
        case 0x4: *acc &= m; nz8(*acc); set(FV, 0); break;       /* AND */
        case 0x5: nz8(*acc & m); set(FV, 0); break;              /* BIT */
        case 0x6: *acc = m; nz8(m); set(FV, 0); break;           /* LDA */
        case 0x8: *acc ^= m; nz8(*acc); set(FV, 0); break;       /* EOR */
        case 0x9: *acc = add8(*acc, m, cpu.cc & FC); break;      /* ADC */
        case 0xA: *acc |= m; nz8(*acc); set(FV, 0); break;       /* ORA */
        case 0xB: *acc = add8(*acc, m, 0); break;                /* ADD */
        default: break;
        }
        return cycles;
    }

    /* 0x20-0x2F: branches */
    if (hi == 2) {
        int8_t off = (int8_t)fetch();
        if (branch_taken(op)) cpu.pc = (uint16_t)(cpu.pc + off);
        return 4;
    }

    switch (op) {
    case 0x01: return 2;                                                     /* NOP */
    case 0x06: cpu.cc = cpu.a | 0xC0; return 2;                              /* TAP */
    case 0x07: cpu.a = cpu.cc | 0xC0; return 2;                              /* TPA */
    case 0x08: cpu.x++; set(FZ, cpu.x == 0); return 4;                       /* INX */
    case 0x09: cpu.x--; set(FZ, cpu.x == 0); return 4;                       /* DEX */
    case 0x0A: set(FV, 0); return 2;                                         /* CLV */
    case 0x0B: set(FV, 1); return 2;                                         /* SEV */
    case 0x0C: set(FC, 0); return 2;                                         /* CLC */
    case 0x0D: set(FC, 1); return 2;                                         /* SEC */
    case 0x0E: set(FI, 0); return 2;                                         /* CLI */
    case 0x0F: set(FI, 1); return 2;                                         /* SEI */
    case 0x10: cpu.a = sub8(cpu.a, cpu.b, 0); return 2;                      /* SBA */
    case 0x11: sub8(cpu.a, cpu.b, 0); return 2;                              /* CBA */
    case 0x16: cpu.b = cpu.a; nz8(cpu.b); set(FV, 0); return 2;              /* TAB */
    case 0x17: cpu.a = cpu.b; nz8(cpu.a); set(FV, 0); return 2;              /* TBA */
    case 0x19: {                                                             /* DAA */
        unsigned a = cpu.a, fix = 0;
        if ((cpu.cc & FH) || (a & 0xF) > 9) fix |= 0x06;
        if ((cpu.cc & FC) || a > 0x99 || (a > 0x8F && (a & 0xF) > 9)) fix |= 0x60;
        a += fix;
        set(FC, (cpu.cc & FC) || (a & 0x100));
        cpu.a = (uint8_t)a; nz8(cpu.a);
        return 2;
    }
    case 0x1B: cpu.a = add8(cpu.a, cpu.b, 0); return 2;                      /* ABA */
    case 0x30: cpu.x = (uint16_t)(cpu.sp + 1); return 4;                     /* TSX */
    case 0x31: cpu.sp++; return 4;                                           /* INS */
    case 0x32: cpu.a = pull(); return 4;                                     /* PULA */
    case 0x33: cpu.b = pull(); return 4;                                     /* PULB */
    case 0x34: cpu.sp--; return 4;                                           /* DES */
    case 0x35: cpu.sp = (uint16_t)(cpu.x - 1); return 4;                     /* TXS */
    case 0x36: push(cpu.a); return 4;                                        /* PSHA */
    case 0x37: push(cpu.b); return 4;                                        /* PSHB */
    case 0x39: cpu.pc = (uint16_t)(pull() << 8); cpu.pc |= pull(); return 5; /* RTS */
    case 0x3B:                                                               /* RTI */
        cpu.cc = pull() | 0xC0; cpu.b = pull(); cpu.a = pull();
        cpu.x = (uint16_t)(pull() << 8); cpu.x |= pull();
        cpu.pc = (uint16_t)(pull() << 8); cpu.pc |= pull();
        return 10;
    case 0x3E: push_all(); cpu.waiting = 1; return 9;                        /* WAI */
    case 0x3F: push_all(); cpu.cc |= FI; cpu.pc = rd16(0xFFFA); return 12;   /* SWI */
    default: return 2; /* not an instruction */
    }
}

/* ======================================================================
 * Commands: the main board sends a 5-bit number and interrupts the sound CPU
 * ====================================================================== */

#define CMD_GAP 700 /* CPU cycles between two commands of one burst */

/* loop() puts commands here, audio() sends them to the board one at a time */
#define QUEUE 64
static volatile uint8_t queue[QUEUE];
static volatile uint32_t queue_in, queue_out;

static void send(int command) {
    uint32_t next = (queue_in + 1) % QUEUE;
    if (next == queue_out) return; /* full */
    queue[queue_in] = (uint8_t)command;
    queue_in = next;
}

/* Game sounds: commands at game frame times (60 per second), as in GWAVE */
typedef struct { uint16_t frame; uint8_t command; } step_t;
typedef struct { const char *name; const step_t *steps; int count; } game_sound_t;

static const step_t s_death[] = { { 0, 17 }, { 8, 17 }, { 16, 23 }, { 100, 19 } };
static const step_t s_bomb[] = { { 0, 17 }, { 4, 17 }, { 8, 17 }, { 12, 17 }, { 16, 17 }, { 20, 17 }, { 24, 23 } };
static const step_t s_planet[] = {
    { 0, 17 }, { 4, 17 }, { 8, 17 }, { 12, 17 }, { 16, 17 }, { 20, 17 }, { 24, 17 }, { 28, 17 },
    { 32, 17 }, { 36, 17 }, { 40, 17 }, { 44, 17 }, { 48, 17 }, { 52, 17 }, { 56, 17 }, { 60, 17 },
    { 64, 20 }, { 68, 17 }, { 74, 17 }, { 78, 23 }, { 88, 23 },
};
static const step_t s_caught[] = { { 0, 8 }, { 10, 8 }, { 20, 8 } };
static const step_t s_suck[] = { { 0, 14 }, { 1, 14 }, { 2, 14 }, { 3, 14 }, { 4, 14 }, { 5, 14 }, { 6, 14 }, { 7, 14 }, { 8, 14 }, { 9, 14 } };
static const step_t s_today[] = { { 0, 27 }, { 0, 1 } }; /* ORGAN TUNE, then tune 1 */
static const step_t s_alltime[] = { { 0, 27 }, { 0, 2 } }; /* ORGAN TUNE, then tune 2 */
static const step_t s_thrust[] = { { 0, 22 }, { 90, 15 }, { 210, 19 } };

#define GAME(n, s) { n, s, (int)(sizeof s / sizeof s[0]) }
static const game_sound_t games[] = {
    GAME("PLAYER DEATH", s_death), GAME("SMART BOMB", s_bomb), GAME("PLANET EXPLODES", s_planet),
    GAME("HUMANOID CAUGHT", s_caught), GAME("LANDER SUCK", s_suck), GAME("TODAY'S HIGH SCORE", s_today),
    GAME("ALL-TIME HIGH SCORE", s_alltime), GAME("THRUST + RELEASE", s_thrust),
};
#define GAME_COUNT (int)(sizeof games / sizeof games[0])

static volatile int game_request = -1; /* set by loop() */
static volatile int game_playing = -1; /* read by loop() */
static int game_step;
static float game_time; /* CPU cycles since the game sound started */
static uint8_t game_queue[8]; /* its commands that wait to be sent (audio() only) */
static int game_count;

/* The sound list for PRESETS and the note keys. ORGAN TUNE (27) and
 * ORGAN NOTE (28) wait for more commands, so the tunes are their own items. */
typedef struct { uint8_t command, tune; const char *name; } sound_t;
static const sound_t sounds[] = {
    { 1, 0, "BOMBER HIT" }, { 2, 0, "START DISTORTO" }, { 3, 0, "ENEMY SHOT" },
    { 4, 0, "SPINNER RISE" }, { 5, 0, "POD HIT" }, { 6, 0, "LANDER HIT" },
    { 7, 0, "BAITER HIT" }, { 8, 0, "HUMANOID CATCH" }, { 9, 0, "MUTANT SHOT" },
    { 10, 0, "START 1 PLAYER" }, { 11, 0, "START 2 PLAYERS" }, { 12, 0, "SWARMER SHOT" },
    { 13, 0, "ED'S SOUND 17" }, { 14, 0, "LANDER SUCK" }, { 15, 0, "BACKGROUND" },
    { 16, 0, "TURBINE STEP" }, { 17, 0, "LIGHTNING" }, { 18, 0, "LASER BALL BONUS" },
    { 19, 0, "SOUND OFF" }, { 20, 0, "LASER" }, { 21, 0, "APPEAR" },
    { 22, 0, "THRUST" }, { 23, 0, "EXPLOSION" }, { 24, 0, "RADIO" },
    { 25, 0, "COIN" }, { 26, 0, "HUMANOID SCREAM" }, { 29, 0, "SAW" },
    { 30, 0, "FREE SHIP" }, { 31, 0, "HUMANOID LANDS" },
    { 27, 1, "ORGAN: PHANTOM" }, { 27, 2, "ORGAN: TOCCATA" },
};
#define SOUND_COUNT (int)(sizeof sounds / sizeof sounds[0])

static void play_sound(int i) {
    send(sounds[i].command);
    if (sounds[i].tune) send(sounds[i].tune);
}

/* ORGAN NOTE: command 28, the oscillator mask in two halves (+1), the note (+2) */
static void play_organ(int mask, int note) {
    send(28);
    send((mask >> 4) + 1);
    send((mask & 15) + 1);
    send(note + 2);
}

/* ======================================================================
 * Audio: run the CPU, turn the DAC into samples
 * ====================================================================== */

static volatile uint32_t audio_blocks;
static float cycles_per_sample;
static float carry_cycles; /* cycles of the last instruction that belong to the next sample */
static uint8_t carry_value;
static float gap_left; /* cycles until the next command may go */
static float hp_x, hp_y, hp_coef; /* the 25 Hz high pass of the output capacitor */
static float lp_y; /* TONE */
static volatile int tone_knob = 12;

#define SCOPE_RING 2048
static uint8_t scope_ring[SCOPE_RING];
static volatile uint32_t scope_pos;

/* Called between instructions: sends a waiting command and runs the game sound */
static void board_events(float cycles) {
    if (gap_left > 0) gap_left -= cycles;

    int request = game_request;
    if (request >= 0) {
        game_request = -1;
        game_playing = request;
        game_step = 0;
        game_time = 0;
    }
    if (game_playing >= 0) {
        const game_sound_t *g = &games[game_playing];
        game_time += cycles;
        while (game_step < g->count && game_time >= g->steps[game_step].frame * FRAME_CYCLES) {
            if (game_count < (int)sizeof game_queue) game_queue[game_count++] = g->steps[game_step].command;
            game_step++;
        }
        if (game_step >= g->count) game_playing = -1;
    }

    if (irq_flag || gap_left > 0) return;
    int command;
    if (game_count > 0) { /* the game sound's own commands first */
        command = game_queue[0];
        memmove(game_queue, game_queue + 1, (size_t)--game_count);
    } else if (queue_out != queue_in) {
        command = queue[queue_out];
        queue_out = (queue_out + 1) % QUEUE;
    } else return;
    port_b = (uint8_t)~command; /* the lines are active low */
    irq_flag = 1;
    gap_left = CMD_GAP;
}

/* The mean DAC value over one output sample, from the cycle-exact DAC writes */
static float next_sample(void) {
    float need = cycles_per_sample;
    float sum = carry_value * carry_cycles;
    float t = carry_cycles;
    carry_cycles = 0;
    while (t < need) {
        uint8_t before = dac; /* a store changes the DAC at the end of the instruction */
        float c = (float)cpu_step();
        board_events(c);
        if (t + c <= need) {
            sum += before * c;
            t += c;
        } else {
            sum += before * (need - t);
            carry_cycles = t + c - need;
            carry_value = before;
            t = need;
        }
    }
    /* The instruction that crossed into the next sample keeps the old DAC value
     * until its end (carry_value); after it, the loop above reads the new one. */
    return sum / need;
}

void audio(int32_t *out, int frames) {
    /* TONE: from about 1.5 kHz (0) to open (12) */
    int tk = tone_knob;
    float lp_coef = tk >= 12 ? 1.0f : 1.0f - expf(-6.2831853f * 1500.0f * powf(1.3f, (float)tk) / FM1_AUDIO_RATE);

    for (int i = 0; i < frames; i++) {
        float level = next_sample(); /* 0 to 255 */
        scope_ring[scope_pos] = (uint8_t)(level + 0.5f);
        scope_pos = (scope_pos + 1) & (SCOPE_RING - 1);

        float x = (level - 128.0f) / 128.0f;
        hp_y = hp_coef * (hp_y + x - hp_x); /* remove DC like the output capacitor */
        hp_x = x;
        lp_y += (hp_y - lp_y) * lp_coef;

        /* 12 dB below full scale, the full level: the FM-1 puts MASTER on the speaker itself */
        float s = lp_y * 0.8f * 0.25f;
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int32_t v = (int32_t)(s * FM1_AUDIO_MAX);
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    audio_blocks++;
}

/* ======================================================================
 * Screen
 * ====================================================================== */

#define RGB(r, g, b) (uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3)
#define BLACK RGB(0, 0, 0)
#define DARK RGB(50, 34, 20)
#define DIM RGB(140, 110, 80)
#define AMBER RGB(255, 170, 40)
#define ORANGE RGB(255, 110, 30)
#define GREEN RGB(120, 255, 120)
#define WHITE RGB(255, 240, 220)
#define CYAN RGB(90, 220, 255)

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour) {
    if (w > 0 && h > 0) fm1_lcd_fill_rect(x, y, w, h, colour);
}

/* A 3 x 5 pixel font at twice the size. Each octal digit is one row, top first;
 * in a row 4 is the left pixel and 1 the right one. */
static const char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.:'/";
static const uint16_t font[] = {
    025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011152,
    055655, 044447, 057755, 065555, 025552, 065644, 025563, 065655, 034216, 072222,
    055557, 055552, 055775, 055255, 055222, 071247, 075557, 026227, 061247, 061216,
    055711, 074616, 034757, 071222, 075757, 075716, 002720, 000700, 000002, 002020,
    022000, 011244,
};

/* Text in a field of `width` characters (8 x 10 pixels each), blank after the text */
static void draw_text(uint32_t x, uint32_t y, const char *text, int width, uint16_t colour) {
    for (int i = 0; i < width; i++, x += 8) {
        fill(x, y, 8, 10, BLACK);
        char c = *text ? *text++ : ' ';
        const char *found = c == ' ' ? 0 : strchr(font_chars, c);
        if (!found) continue;
        uint16_t glyph = font[found - font_chars];
        for (int row = 0; row < 5; row++)
            for (int column = 0; column < 3; column++)
                if ((glyph >> (3 * (4 - row) + 2 - column)) & 1u)
                    fill(x + column * 2, y + row * 2, 2, 2, colour);
    }
}

static void two_digits(char *p, int v) { p[0] = (char)('0' + v / 10 % 10); p[1] = (char)('0' + v % 10); }

/* Layout */
#define TITLE_Y 4
#define MODE_Y 22
#define SOUND_Y 40
#define GAME_Y 56
#define NOW_Y 74
#define SCOPE_TOP 92
#define SCOPE_H 82
#define STATUS_Y 182
#define HELP_Y 200

/* ======================================================================
 * Program
 * ====================================================================== */

typedef struct { const char *name; uint8_t pattern; } stop_t;
/* bit 7 is the note itself, each lower bit one octave higher */
static const stop_t stops[] = {
    { "TUNES", 0xF8 }, { "FULL", 0xFE }, { "FLUTE", 0x80 },
    { "OCTAVE", 0xC0 }, { "HOLLOW", 0xA8 }, { "REED", 0xE0 },
};
#define STOP_COUNT (int)(sizeof stops / sizeof stops[0])

static int organ_mode;
static int octave; /* ORGAN mode: -2 to +1 */
static int stop_index;
static int sound_index = 5; /* LANDER HIT */
static int game_index;
static char now_text[31] = "READY";
static uint32_t held_notes, last_held, midi_notes;
static int organ_key = -999; /* the organ key that sounds */
static uint32_t shown_leds;
static int shown_bg = -1, shown_game = -2;
static int keys_lit[FM1_NOTE_KEYS];

static void draw_mode(void) {
    char text[31];
    if (!organ_mode) {
        draw_text(4, MODE_Y, "KEYS: SFX 1-27", 29, GREEN);
        return;
    }
    strcpy(text, "KEYS: ORGAN OCT  0 ");
    text[16] = octave < 0 ? '-' : octave > 0 ? '+' : ' ';
    text[17] = (char)('0' + (octave < 0 ? -octave : octave));
    strcat(text, stops[stop_index].name);
    draw_text(4, MODE_Y, text, 29, CYAN);
}

static void draw_sound(void) {
    char text[31] = "PLAY 00 ";
    two_digits(text + 5, sounds[sound_index].command);
    strcat(text, sounds[sound_index].name);
    draw_text(4, SOUND_Y, text, 29, AMBER);
}

static void draw_game(void) {
    char text[31] = "SEQ  ";
    strcat(text, games[game_index].name);
    draw_text(4, GAME_Y, text, 29, game_playing == game_index ? WHITE : AMBER);
}

static void draw_now(void) { draw_text(4, NOW_Y, now_text, 29, WHITE); }

static void set_now(const char *prefix, int number, const char *name) {
    strcpy(now_text, prefix);
    if (number >= 0) {
        size_t n = strlen(now_text);
        two_digits(now_text + n, number);
        now_text[n + 2] = ' ';
        now_text[n + 3] = 0;
    }
    strncat(now_text, name, sizeof now_text - strlen(now_text) - 1);
    draw_now();
}

static void draw_status(void) {
    char text[31];
    int bg1 = ram[4], bg2 = ram[5] & 0x7F;
    if (bg1) strcpy(text, "BACKGROUND: ON");
    else if (bg2) {
        strcpy(text, "TURBINE: 00");
        two_digits(text + 9, bg2);
    } else strcpy(text, "BACKGROUND: OFF");
    draw_text(4, STATUS_Y, text, 29, bg1 || bg2 ? GREEN : DIM);
}

static void draw_help(void) {
    draw_text(4, HELP_Y, "SCL MODE  HOME OFF", 29, DIM);
    draw_text(4, HELP_Y + 14, "PRESETS+PLAY  SELECT+SEQ", 29, DIM);
    draw_text(4, HELP_Y + 28, "ALG STOPS  KNOB1 TONE", 29, DIM);
}

/* The DAC as the ROM writes it, 0 to 255, one column per sample */
static uint8_t scope_top[FM1_LCD_WIDTH], scope_bottom[FM1_LCD_WIDTH];
static uint32_t scope_block;

static void draw_scope(void) {
    const uint32_t mask = SCOPE_RING - 1;
    uint32_t pos = scope_pos;
    uint32_t start = (pos - FM1_LCD_WIDTH) & mask;
    for (uint32_t back = 0; back < 1024; back++) { /* trigger on a rising edge through the middle */
        uint32_t i = (pos - FM1_LCD_WIDTH - back) & mask;
        if (scope_ring[(i - 1) & mask] < 128 && scope_ring[i] >= 128) { start = i; break; }
    }
    int previous = SCOPE_TOP + SCOPE_H - 1 - scope_ring[(start - 1) & mask] * (SCOPE_H - 2) / 255;
    for (uint32_t x = 0; x < FM1_LCD_WIDTH; x++) {
        int y = SCOPE_TOP + SCOPE_H - 1 - scope_ring[(start + x) & mask] * (SCOPE_H - 2) / 255;
        int top = y < previous ? y : previous;
        int bottom = (y > previous ? y : previous) + 2;
        previous = y;
        if (top == scope_top[x] && bottom == scope_bottom[x]) continue;
        fill(x, scope_top[x], 1, scope_bottom[x] - scope_top[x], BLACK);
        if (x % 4 == 0) fill(x, SCOPE_TOP + SCOPE_H / 2, 1, 1, DARK);
        fill(x, top, 1, bottom - top, ORANGE);
        scope_top[x] = (uint8_t)top;
        scope_bottom[x] = (uint8_t)bottom;
    }
}

void setup(void) {
    cycles_per_sample = CPU_CLOCK / FM1_AUDIO_RATE;
    float rc = 8.0f * 800e-6f; /* speaker and output capacitor: 25 Hz */
    hp_coef = rc / (rc + 1.0f / FM1_AUDIO_RATE);
    cpu_reset();

    fill(0, 0, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, BLACK);
    draw_text(4, TITLE_Y, "GWAVE", 5, ORANGE);
    draw_text(52, TITLE_Y, "DEFENDER SOUND ROM", 18, DIM);
    fill(0, 16, FM1_LCD_WIDTH, 2, ORANGE);
    fill(0, SCOPE_TOP - 4, FM1_LCD_WIDTH, 1, DARK);
    fill(0, SCOPE_TOP + SCOPE_H + 2, FM1_LCD_WIDTH, 1, DARK);
    for (uint32_t x = 0; x < FM1_LCD_WIDTH; x++) {
        scope_top[x] = SCOPE_TOP + SCOPE_H / 2;
        scope_bottom[x] = SCOPE_TOP + SCOPE_H / 2;
    }
    draw_mode();
    draw_sound();
    draw_game();
    draw_now();
    draw_help();
}

/* A note key (or MIDI note) n: 0 is F3. Returns 0 when it has no organ note. */
static int organ_note(int n, int *mask, int *note) {
    int k = n + 9; /* the ROM's scale starts at A flat: F is note 9 */
    int block = k >= 0 ? k / 12 : -((11 - k) / 12);
    int top_bit = 5 - block - octave; /* the bit of TEMPB that gives the note */
    if (top_bit < 0 || top_bit > 7) return 0;
    *note = k - block * 12;
    *mask = (stops[stop_index].pattern >> (7 - top_bit)) & 0xFF;
    return *mask != 0;
}

static const char *const note_names[12] = { "AF", "A", "BF", "B", "C", "CS", "D", "EF", "E", "F", "FS", "G" };

static void organ_press(int n) {
    int mask, note;
    if (!organ_note(n, &mask, &note)) return;
    play_organ(mask, note);
    organ_key = n;
    set_now("ORGAN ", -1, note_names[note]);
}

static void read_midi(void) {
    uint32_t packet;
    while (fm1_midi_take(&packet)) {
        uint32_t type = FM1_MIDI_STATUS(packet) & 0xF0u;
        int n = (int)FM1_MIDI_DATA1(packet) - 53;
        uint32_t velocity = FM1_MIDI_DATA2(packet);
        if (n < 0 || n >= (int)FM1_NOTE_KEYS) continue;
        if (type == 0x90u && velocity > 0) midi_notes |= BIT(n);
        else if (type == 0x80u || type == 0x90u) midi_notes &= ~BIT(n);
    }
}

static void read_keys(void) {
    read_midi();
    held_notes = fm1_in.notes | midi_notes;
    uint32_t pressed = held_notes & ~last_held;
    uint32_t released = last_held & ~held_notes;
    last_held = held_notes;

    for (int n = 0; n < (int)FM1_NOTE_KEYS; n++) {
        if (!((pressed >> n) & 1u)) continue;
        if (organ_mode) organ_press(n);
        else if (n < SOUND_COUNT) {
            play_sound(n);
            set_now("", sounds[n].command, sounds[n].name);
        }
    }
    /* ORGAN: the note stops with its key, or goes to a key that is still down */
    if (organ_mode && organ_key >= 0 && ((released >> organ_key) & 1u)) {
        if (held_notes) {
            int n = 31 - __builtin_clz(held_notes); /* the highest key down */
            organ_press(n);
        } else {
            send(0); /* no sound: the ROM goes back to the background or to silence */
            organ_key = -999;
        }
    }
    for (int n = 0; n < (int)FM1_NOTE_KEYS; n++) {
        int lit = (held_notes >> n) & 1u;
        if (lit != keys_lit[n]) fm1_led_key(FM1_KEY_NOTE0 + n, lit);
        keys_lit[n] = lit;
    }
}

static void read_buttons(void) {
    uint32_t pressed = fm1_input_edges(0);
    if (pressed & BIT(FM1_BTN_SCL)) {
        organ_mode = !organ_mode;
        if (organ_key >= 0) { send(0); organ_key = -999; }
        draw_mode();
    }
    if ((pressed & BIT(FM1_BTN_OCT_DOWN)) && octave > -2) { octave--; draw_mode(); }
    if ((pressed & BIT(FM1_BTN_OCT_UP)) && octave < 1) { octave++; draw_mode(); }
    if (pressed & BIT(FM1_BTN_PLAY)) {
        play_sound(sound_index);
        set_now("", sounds[sound_index].command, sounds[sound_index].name);
    }
    if (pressed & BIT(FM1_BTN_SEQ)) {
        game_request = game_index;
        set_now("GAME ", -1, games[game_index].name);
    }
    if (pressed & BIT(FM1_BTN_HOME)) {
        game_playing = -1;
        game_request = -1;
        organ_key = -999;
        send(19); /* SOUND OFF */
        set_now("", 19, "SOUND OFF");
    }

    uint32_t leds = fm1_in.buttons & 0x3FFFu;
    if (organ_mode) leds |= BIT(FM1_BTN_SCL);
    if (octave < 0) leds |= BIT(FM1_BTN_OCT_DOWN);
    if (octave > 0) leds |= BIT(FM1_BTN_OCT_UP);
    if (game_playing >= 0) leds |= BIT(FM1_BTN_SEQ);
    for (uint32_t id = 0; id < FM1_KEY_NOTE0; id++)
        if (((leds ^ shown_leds) >> id) & 1u) fm1_led_key(id, (leds >> id) & 1u);
    shown_leds = leds;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void read_knobs(void) {
    int turned;
    if ((turned = fm1_enc_take(FM1_ENC_PRESETS)) != 0) {
        sound_index = clamp(sound_index + turned, 0, SOUND_COUNT - 1);
        draw_sound();
    }
    if ((turned = fm1_enc_take(FM1_ENC_SELECT)) != 0) {
        game_index = clamp(game_index + turned, 0, GAME_COUNT - 1);
        draw_game();
    }
    if ((turned = fm1_enc_take(FM1_ENC_ALGORITHM)) != 0) {
        stop_index = clamp(stop_index + turned, 0, STOP_COUNT - 1);
        draw_mode();
    }
    if ((turned = fm1_enc_take(FM1_ENC_KNOB1)) != 0) {
        tone_knob = clamp(tone_knob + turned, 0, 12);
        char text[12] = "TONE 00";
        two_digits(text + 5, tone_knob);
        set_now("", -1, text);
    }
    for (uint32_t e = FM1_ENC_KNOB2; e <= FM1_ENC_KNOB4; e++) fm1_enc_take(e);
}

void loop(void) {
    read_keys();
    read_buttons();
    read_knobs();

    int bg = ram[4] << 8 | ram[5];
    if (bg != shown_bg) { shown_bg = bg; draw_status(); }
    if (game_playing != shown_game) { shown_game = game_playing; draw_game(); }

    if (audio_blocks - scope_block >= 4) {
        scope_block = audio_blocks;
        draw_scope();
    }
}

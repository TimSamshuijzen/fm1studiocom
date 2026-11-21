#include "fm1.h"
/* LED test: FX glow, SCL mid, ENV on, LFO breath, PLAY red + green; keys F3..A3: off, glow, mid, on, breath.
   OCT+ switches DIM LO, REC switches PLAY's green LED, SEQ its red one. */
static int low, green = 1, red = 1;
void setup(void) {
  fm1_led_level(FM1_BTN_FX, FM1_LED_GLOW);
  fm1_led_level(FM1_BTN_SCL, FM1_LED_MID);
  fm1_led_level(FM1_BTN_ENV, FM1_LED_ON);
  fm1_led_level(FM1_BTN_LFO, FM1_LED_BREATH);
  fm1_led_key(FM1_BTN_PLAY, green);
  fm1_led_key(FM1_LED_PLAY_RED, red);
  for (uint32_t k = 0; k < 5; k++) fm1_led_level(FM1_KEY_NOTE0 + k, k);
  fm1_lcd_fill_rect(0, 0, 240, 240, 0x0000);
}
void audio(int32_t *out, int frames) { for (int i = 0; i < 2 * frames; i++) out[i] = 0; }
void loop(void) {
  uint32_t released, pressed = fm1_input_edges(&released);
  if (pressed & (1u << FM1_BTN_OCT_UP)) { low = !low; fm1_led_dim_low(low); }
  if (pressed & (1u << FM1_BTN_REC)) { green = !green; fm1_led_key(FM1_BTN_PLAY, green); }
  if (pressed & (1u << FM1_BTN_SEQ)) { red = !red; fm1_led_key(FM1_LED_PLAY_RED, red); }
}
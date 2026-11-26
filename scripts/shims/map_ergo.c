/* DDJ-ERGO-specific controls; shared Pioneer controls are in map_flx4.c. */
#include <sound/asequencer.h>

#include "ctrl_map.h"
#include "map_pioneer_shared.h"
#include "midi_io.h"
#include "pitch_state.h"
#include "rbp_abi.h"
#include "rbp_bridge.h"
#include "rbp_key.h"
#include "rbp_vu.h"
#include "rx_panel_led.h"
#include "shimutil.h"


//ddj ergo mapping notes for wip

/*

timeout 30s aseqdump -p 'PIONEER DDJ-ERGO:0'

DECK A
----
vinyl 'pulse mode' (vinyl jog/cdj mode?)
Source  Event                  Ch  Data
 20:0   Note on                 0, note 21, velocity 127
 20:0   Note on                 2, note 21, velocity 127
 20:0   Note off                0, note 21
 20:0   Note off                2, note 21
---

DECK B
----
vinyl 'pulse mode' (vinyl jog/cdj mode?)
Source  Event                  Ch  Data

---
 20:0   Note on                 1, note 21, velocity 127
 20:0   Note on                 3, note 21, velocity 127
 20:0   Note off                1, note 21
 20:0   Note off                3, note 21

*/



enum {
     ERGO_MIX = 6,
     ERGO_BROWSE = 101,
     ERGO_BROWSE_SHIFT = 102,
     ERGO_BACK = 105,
     ERGO_MENU = 107,
     ERGO_DECK_CD = 114,
     ERGO_LOOP_PRESS = 20,
     ERGO_LOOP_TURN = 19,
     ERGO_BEAT_PREV = 64,
     ERGO_BEAT_NEXT = 66,
     ERGO_VINYL = 21,
     ERGO_JOG_TOUCH = 54,
     ERGO_JOG_TOUCH_ALT = 55,
     ERGO_CUE_1 = 84,
     ERGO_CUE_2 = 85,
     ERGO_FX1 = 4,
     ERGO_FX2 = 5,
     ERGO_FX_ON = 67,
     ERGO_FILTER_A = 27,
};

static int loop_size[2] = { 7, 7 };
static int loop_down[2];
static unsigned jog_touch_down[2];
static unsigned hotcue_down[2], hotcue_shift_down[2];
static int fx_mix_msb, fx_mix_lsb, fx_mix_last, fx_on_down;
static int fx_target_down[8];
static int fx_select_down[2], fx_select_pos;
static int fx_select_led[2], fx_target_led;
static int panel_led_last[33];
static unsigned panel_led_ticks;
static int panel_led_disabled;
static const struct { unsigned short beats, divisor; } loop_sizes[] = {
     {1, 32}, {1, 16}, {1, 8}, {1, 4}, {1, 2}, {1, 1},
     {2, 1}, {4, 1}, {8, 1}, {16, 1}, {32, 1}
};

static void ergo_reset(void)
{
     loop_size[0] = loop_size[1] = 7;
     loop_down[0] = loop_down[1] = 0;
     jog_touch_down[0] = jog_touch_down[1] = 0;
     hotcue_down[0] = hotcue_down[1] = 0;
     hotcue_shift_down[0] = hotcue_shift_down[1] = 0;
     fx_mix_msb = -1;
     fx_mix_lsb = 0;
     fx_mix_last = -1;
     fx_on_down = 0;
     fx_select_pos = -1;
     __atomic_store_n(&fx_target_led, 0, __ATOMIC_RELAXED);
     for (int i = 0; i < 2; i++) {
          fx_select_down[i] = 0;
          __atomic_store_n(&fx_select_led[i], 0, __ATOMIC_RELAXED);
     }
     for (int i = 0; i < 8; i++)
          fx_target_down[i] = 0;
     for (int i = 0; i < 33; i++)
          panel_led_last[i] = -1;
     panel_led_ticks = 0;
     panel_led_disabled = env_on("LED_DISABLE", 0);
}

void ergo_add_deck(int deck)
{
     int sch = deck + 1;
     add_note(deck, 26, 0x4109, sch);
     add_note(deck, 27, 0x4108, sch);
     add_note(deck, 92, 0x4112, sch);
     add_note(deck, ERGO_VINYL, 0x4104, sch);
}

void ergo_add_global(int rx)
{
     if (rx) {
          add_note(ERGO_MIX, ERGO_BROWSE, 0x0208, CH_GLOBAL);
          add_note(ERGO_MIX, ERGO_BROWSE_SHIFT, 0x4488, 0);
          add_note(ERGO_MIX, ERGO_CUE_1, 0x5014, 1);
          add_note(ERGO_MIX, ERGO_CUE_2, 0x5014, 2);
     } else {
          add_note(2, ERGO_DECK_CD, K_LOAD, 1);
          add_note(3, ERGO_DECK_CD, K_LOAD, 2);
     }
     add_note(ERGO_MIX, ERGO_BACK, rx ? 0x4206 : K_BACK, CH_GLOBAL);
     add_note(ERGO_MIX, ERGO_MENU, rx ? 0x020a : K_MENU, CH_GLOBAL);
}

static void loop_turn(int deck, int value)
{
     if (value < 1 || value > 127 || value == 64)
          return;
     int steps = value < 64 ? -value : 128 - value;
     int active = rx_loop_is_active(deck);
     if (active < 0)
          return;
     int direction = steps > 0 ? 1 : -1;
     int count = steps > 0 ? steps : -steps;
     if (count > 10)
          count = 10;
     while (count--) {
          int next = loop_size[deck] + direction;
          if (active) {
               int result = rx_loop_resize(deck, direction > 0);
               if (result <= 0) {
                    klog("knobshim2: RX loop resize deck%d rejected (%d)\n",
                         deck + 1, result);
                    break;
               }
          }
          if (next >= 0 && next < (int)(sizeof loop_sizes / sizeof loop_sizes[0]))
               loop_size[deck] = next;
     }
}

static void loop_press(int deck, int on)
{
     if (!on) {
          loop_down[deck] = 0;
          return;
     }
     if (loop_down[deck])
          return;
     loop_down[deck] = 1;
     int active = rx_loop_is_active(deck);
     if (active < 0)
          return;
     if (active) {
          send_rx_key(0x4110, OP_PRESS, deck + 1, 0);
          send_rx_key(0x4110, OP_RELEASE, deck + 1, 0);
     } else {
          int idx = loop_size[deck];
          int result = rx_loop_start(deck, loop_sizes[idx].beats, loop_sizes[idx].divisor);
          if (result <= 0)
               klog("knobshim2: RX auto loop deck%d start rejected (%d)\n",
                    deck + 1, result);
     }
}

static int mixer_cc(int ch, int cc, int val)
{
     static struct mixer_cc {
          int cc, key, channel, msb, lsb, last;
     } controls[] = {
          { 0, 0x4405, 0, -1, 0, -1 },
          { 1, 0x4402, 0, -1, 0, -1 },
          { 2, 0x4403, 0, -1, 0, -1 },
          { 3, 0x5007, 1, -1, 0, -1 }, { 4, 0x5007, 2, -1, 0, -1 },
          { 7, 0x5008, 1, -1, 0, -1 }, { 8, 0x5008, 2, -1, 0, -1 },
          {11, 0x5009, 1, -1, 0, -1 }, {12, 0x5009, 2, -1, 0, -1 },
          {15, 0x500a, 1, -1, 0, -1 }, {16, 0x500a, 2, -1, 0, -1 },
          {19, 0x5011, 1, -1, 0, -1 }, {21, 0x5011, 2, -1, 0, -1 },
          {31, 0x600f, 0, -1, 0, -1 },
     };

     if (ch != ERGO_MIX)
          return 0;
     for (size_t i = 0; i < sizeof controls / sizeof controls[0]; i++) {
          struct mixer_cc *c = &controls[i];
          if (cc != c->cc && cc != c->cc + 32)
               continue;
          if (cc == c->cc)
               c->msb = val;
          else
               c->lsb = val;
          if (c->msb < 0)
               return 1;
          int v = ((c->msb * 128 + c->lsb) * 1023 + 8191) / 16383;
          if (v == c->last)
               return 1;
          c->last = v;
          if (c->key == 0x5011) {
               g_fader[c->channel] = v;
               g_fader_seen[c->channel] = 1;
          }
          send_rx_key_f(c->key, 4, c->channel, v, (float)v / 1023.0f);
          return 1;
     }
     return 0;
}

void ergo_pitch(int deck, int pos, int reverse, int verbose)
{
     int neg = (pos * 1023 + 8191) / 16383;
     if (reverse)
          neg = 1023 - neg;
     int positive = 1023 - neg;
     int delta = positive - neg;
     int span = delta >= 0 ? positive - 3 : 1020 - positive;
     if (span < 1)
          span = 1;
     if (delta > span)
          delta = span;
     if (delta < -span)
          delta = -span;
     float norm = (float)delta / (float)span;
     pitch_state_set(deck, norm);
     send_rx_key_fl(0x410a, 4, deck + 1, neg, norm, positive);
     if (verbose)
          klog("knobshim2: RX ERGO pitch ch%d pos=0x%04x -> norm=%.3f\n",
               deck, pos, (double)norm);
}

static int ergo_event(const struct snd_seq_event *ev)
{
     if (!rbp_on_rx_profile())
          return 0;
     if (ev->type == SNDRV_SEQ_EVENT_CONTROLLER) {
          int ch = ev->data.control.channel;
          int cc = ev->data.control.param;
          if (ch == ERGO_MIX && (cc == ERGO_FILTER_A || cc == ERGO_FILTER_A + 32)) {
               if (cc == ERGO_FILTER_A)
                    fx_mix_msb = ev->data.control.value;
               else
                    fx_mix_lsb = ev->data.control.value;
               if (fx_mix_msb >= 0) {
                    int v = ((fx_mix_msb * 128 + fx_mix_lsb) * 1023 + 8191) / 16383;
                    if (v != fx_mix_last) {
                         fx_mix_last = v;
                         send_rx_key_f(0x448b, 4, 0, v, (float)v / 1023.0f);
                    }
               }
               return 1;
          }
          if ((ch == ERGO_FX1 || ch == ERGO_FX2) &&
              (cc == 0 || cc == 2 || cc == 4 || cc == 6 ||
               cc == 32 || cc == 34 || cc == 36 || cc == 38))
               return 1;
          if (mixer_cc(ch, cc, ev->data.control.value))
               return 1;
          if ((ch == 0 || ch == 1) && cc == ERGO_LOOP_TURN) {
               loop_turn(ch, ev->data.control.value);
               return 1;
          }
     } else if (ev->type == SNDRV_SEQ_EVENT_NOTEON || ev->type == SNDRV_SEQ_EVENT_NOTEOFF) {
          int ch = ev->data.note.channel;
          int note = ev->data.note.note;
          int on = ev->type == SNDRV_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0;
          if (ch == 0 && (note == ERGO_BEAT_PREV || note == ERGO_BEAT_NEXT)) {
               /* RX native keys, found by probing; the RX3's 0x4490/0x4491 do nothing here. */
               if (on)
                    send_rx_key_fl(note == ERGO_BEAT_NEXT ? 0x4494 : 0x4493,
                                   OP_PRESS, CH_GLOBAL, 1, 0.0f, 0);
               return 1;
          }
          if ((ch == 0 || ch == 1) &&
              ((note >= 46 && note <= 49) || (note >= 95 && note <= 98))) {
               int shifted = note >= 95;
               int pad = note - (shifted ? 95 : 46);
               unsigned bit = 1u << pad;
               unsigned *held = shifted ? &hotcue_shift_down[ch] : &hotcue_down[ch];
               if (on && !(*held & bit)) {
                    int leds = rx_panel_leds();
                    if (leds < 0) {
                         klog("knobshim2: ERGO hot cue deck%d: RX panel state unavailable\n", ch + 1);
                         return 1;
                    }
                    if ((shifted && hotcue_down[ch]) || (!shifted && hotcue_shift_down[ch])) {
                         klog("knobshim2: ERGO hot cue deck%d: conflicting shifted pad held\n", ch + 1);
                         return 1;
                    }
                    if (!(hotcue_down[ch] | hotcue_shift_down[ch]) &&
                        !(leds & (ch ? RX_PANEL_HOT_CUE_MODE_2 : RX_PANEL_HOT_CUE_MODE_1))) {
                         send_rx_key(0x4113, OP_PRESS, ch + 1, 0);
                         send_rx_key(0x4113, OP_RELEASE, ch + 1, 0);
                    }
                    if (shifted && !hotcue_shift_down[ch])
                         send_rx_key(0x4103, OP_PRESS, ch + 1, 0);
                    *held |= bit;
                    send_rx_key(0x4117 + pad, OP_PRESS, ch + 1, 0);
               } else if (!on && (*held & bit)) {
                    *held &= ~bit;
                    send_rx_key(0x4117 + pad, OP_RELEASE, ch + 1, 0);
                    if (shifted && !hotcue_shift_down[ch])
                         send_rx_key(0x4103, OP_RELEASE, ch + 1, 0);
               }
               return 1;
          }
          if ((ch == 0 || ch == 1) &&
              (note == ERGO_JOG_TOUCH || note == ERGO_JOG_TOUCH_ALT)) {
               unsigned mask = 1u << (note - ERGO_JOG_TOUCH);
               unsigned before = jog_touch_down[ch];
               unsigned after = on ? before | mask : before & ~mask;
               jog_touch_down[ch] = after;
               if (!!before != !!after)
                    send_rx_key(0x430e, after ? OP_PRESS : OP_RELEASE, ch + 1, 0);
               return 1;
          }
          if ((ch == ERGO_FX1 || ch == ERGO_FX2) &&
              (note == ERGO_FX_ON || (note >= 71 && note <= 73))) {
               if (ch == ERGO_FX1 && note == ERGO_FX_ON && on != fx_on_down) {
                    fx_on_down = on;
                    send_rx_key(0x4487, on ? OP_PRESS : OP_RELEASE, 0, 0);
               } else if (ch == ERGO_FX1 && (note == 71 || note == 72)) {
                    int idx = note - 71;
                    if (on != fx_select_down[idx]) {
                         fx_select_down[idx] = on;
                         __atomic_store_n(&fx_select_led[idx], on, __ATOMIC_RELAXED);
                         if (on) {
                              if (fx_select_pos < 0)
                                   fx_select_pos = 0;
                              else
                                   fx_select_pos = (fx_select_pos + (idx ? 1 : 7)) % 8;
                              send_rx_key(0x448d, 4, 0, fx_select_pos);
                         }
                    }
               }
               return 1;
          }
          if (ch == ERGO_MIX && note >= 76 && note <= 83) {
               if ((note <= 78 || note == 80) && on != fx_target_down[note - 76]) {
                    fx_target_down[note - 76] = on;
                    if (on) {
                         int target, led_mask;
                         if ((note == 76 || note == 80) &&
                             fx_target_down[0] && fx_target_down[4]) {
                              target = 5;
                              led_mask = (1 << 0) | (1 << 4);
                         } else {
                              static const int targets[] = {0, 1, 5, 0, 1};
                              target = targets[note - 76];
                              led_mask = 1 << (note - 76);
                         }
                         __atomic_store_n(&fx_target_led, led_mask, __ATOMIC_RELAXED);
                         send_rx_key(0x448e, 4, 0, target);
                    }
               }
               return 1;
          }
          if ((ch == 0 || ch == 1) && ev->data.note.note == ERGO_LOOP_PRESS) {
               loop_press(ch, on);
               return 1;
          }
     }
     return 0;
}

static void ergo_build(void)
{
     ergo_reset();
     pioneer_build(PIONEER_ERGO);
}

static void ergo_dispatch(const struct snd_seq_event *ev)
{
     if (!ergo_event(ev))
          pioneer_event(ev);
}

static void ergo_tick(void)
{
     pioneer_tick();
     if (!rbp_on_rx_profile() || panel_led_disabled)
          return;
     int leds = rx_panel_leds();
     int force = (++panel_led_ticks % 5) == 0;
     int target = __atomic_load_n(&fx_target_led, __ATOMIC_RELAXED);
     static const int channels[] = {6, 6, 0, 1, 4, 4, 4, 6, 6, 6, 6, 0, 0, 1, 1, 0, 1,
                                    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1};
     static const int notes[] = {84, 85, 88, 88, 67, 71, 72, 76, 77, 78, 80, 16, 17, 16, 17, 26, 26,
                                 46, 47, 48, 49, 46, 47, 48, 49,
                                 95, 96, 97, 98, 95, 96, 97, 98};
     for (int i = 0; i < 33; i++) {
          int on;
          if (i < 5 || i >= 11) {
               if (leds < 0)
                    continue;
               int bit = i < 5 ? i : i < 25 ? i - 6 : i - 14;
               on = (leds >> bit) & 1;
               if (i >= 17 &&
                   !(leds & (channels[i] == 0 ? RX_PANEL_HOT_CUE_MODE_1 : RX_PANEL_HOT_CUE_MODE_2)))
                    on = 0;
          } else if (i < 7) {
               on = __atomic_load_n(&fx_select_led[i - 5], __ATOMIC_RELAXED);
          } else {
               on = (target & (1 << (notes[i] - 76))) != 0;
          }
          if ((force || panel_led_last[i] != on) &&
              midi_note(channels[i], notes[i], on ? 127 : 0))
               panel_led_last[i] = on;
     }
}

const struct ctrl_map map_ergo = {
     "ergo", ergo_build, pioneer_startup, ergo_dispatch, ergo_tick,
     NULL, NULL, &pioneer_flx4_leds,
};

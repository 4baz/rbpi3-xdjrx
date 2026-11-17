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
     ERGO_BACK = 105,
     ERGO_MENU = 107,
     ERGO_DECK_CD = 114,
     ERGO_LOOP_PRESS = 20,
     ERGO_LOOP_TURN = 19,
     ERGO_VINYL = 21,
     ERGO_CUE_1 = 84,
     ERGO_CUE_2 = 85,
};

static int loop_size[2] = { 7, 7 };
static int loop_down[2];
static int cue_led_last[2];
static unsigned cue_led_ticks;
static int cue_led_disabled;
static const struct { unsigned short beats, divisor; } loop_sizes[] = {
     {1, 32}, {1, 16}, {1, 8}, {1, 4}, {1, 2}, {1, 1},
     {2, 1}, {4, 1}, {8, 1}, {16, 1}, {32, 1}
};

static void ergo_reset(void)
{
     loop_size[0] = loop_size[1] = 7;
     loop_down[0] = loop_down[1] = 0;
     cue_led_last[0] = cue_led_last[1] = -1;
     cue_led_ticks = 0;
     cue_led_disabled = env_on("LED_DISABLE", 0);
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
          if (mixer_cc(ch, cc, ev->data.control.value))
               return 1;
          if ((ch == 0 || ch == 1) && cc == ERGO_LOOP_TURN) {
               loop_turn(ch, ev->data.control.value);
               return 1;
          }
     } else if (ev->type == SNDRV_SEQ_EVENT_NOTEON || ev->type == SNDRV_SEQ_EVENT_NOTEOFF) {
          int ch = ev->data.note.channel;
          if ((ch == 0 || ch == 1) && ev->data.note.note == ERGO_LOOP_PRESS) {
               loop_press(ch, ev->type == SNDRV_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0);
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
     if (!rbp_on_rx_profile() || cue_led_disabled)
          return;
     int cue = rx_panel_cue_leds();
     if (cue < 0)
          return;
     int force = (++cue_led_ticks % 5) == 0;
     for (int deck = 0; deck < 2; deck++) {
          int on = (cue >> deck) & 1;
          if ((force || cue_led_last[deck] != on) &&
              midi_note(ERGO_MIX, ERGO_CUE_1 + deck, on ? 127 : 0))
               cue_led_last[deck] = on;
     }
}

const struct ctrl_map map_ergo = {
     "ergo", ergo_build, pioneer_startup, ergo_dispatch, ergo_tick,
     NULL, NULL, &pioneer_flx4_leds,
};

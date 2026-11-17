#ifndef RBPI4B_MAP_PIONEER_SHARED_H
#define RBPI4B_MAP_PIONEER_SHARED_H

#include "ctrl_map.h"

enum pioneer_profile {
     PIONEER_FLX4,
     PIONEER_DDJ400,
     PIONEER_ERGO,
};

void pioneer_build(enum pioneer_profile profile);
void pioneer_startup(void);
void pioneer_event(const struct snd_seq_event *ev);
void pioneer_tick(void);
extern const struct led_notes pioneer_flx4_leds;

void ergo_add_deck(int deck);
void ergo_add_global(int rx);
void ergo_pitch(int deck, int pos, int reverse, int verbose);

#endif

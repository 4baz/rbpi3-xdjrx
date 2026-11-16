#ifndef RBPI4B_RX_PANEL_H
#define RBPI4B_RX_PANEL_H

#include <stddef.h>
#include <stdint.h>

#define RX_PANEL_FRAME_SIZE 100

struct rx_panel_state {
    char snapshot[2048];
    unsigned pulse_ticks;
};

void rx_panel_frame(struct rx_panel_state *state, const char *snapshot,
                    uint8_t frame[RX_PANEL_FRAME_SIZE]);
int rx_panel_open(void);

#endif

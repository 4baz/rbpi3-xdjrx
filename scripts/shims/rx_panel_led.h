#ifndef RBPI4B_RX_PANEL_LED_H
#define RBPI4B_RX_PANEL_LED_H

#include <stddef.h>
#include <stdint.h>

enum {
    RX_PANEL_CUE_1 = 1 << 0,
    RX_PANEL_CUE_2 = 1 << 1,
    RX_PANEL_SYNC_1 = 1 << 2,
    RX_PANEL_SYNC_2 = 1 << 3,
    RX_PANEL_BEAT_FX = 1 << 4,
    RX_PANEL_LOOP_IN_1 = 1 << 5,
    RX_PANEL_LOOP_OUT_1 = 1 << 6,
    RX_PANEL_LOOP_IN_2 = 1 << 7,
    RX_PANEL_LOOP_OUT_2 = 1 << 8,
    RX_PANEL_MASTER_TEMPO_1 = 1 << 9,
    RX_PANEL_MASTER_TEMPO_2 = 1 << 10,
};

int rx_panel_led_decode(const uint8_t *frame, size_t length);

#endif

#ifndef RBPI4B_RX_PANEL_LED_H
#define RBPI4B_RX_PANEL_LED_H

#include <stddef.h>
#include <stdint.h>

int rx_panel_cue_decode(const uint8_t *frame, size_t length);

#endif

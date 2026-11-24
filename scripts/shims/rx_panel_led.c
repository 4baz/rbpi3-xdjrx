#include "rx_panel_led.h"

int rx_panel_led_decode(const uint8_t *frame, size_t length)
{
    unsigned crc = 0xffff;

    if (!frame || length < 24)
        return -1;
    for (unsigned i = 0; i < 20; i += 4) {
        for (int j = 3; j >= 0; j--) {
            crc ^= frame[i + j];
            for (int bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
        }
    }
    crc ^= 0xffff;
    if (crc != (unsigned)(frame[22] | (frame[23] << 8)))
        return -1;
    return ((frame[10] & 0x20) ? RX_PANEL_CUE_1 : 0) |
           ((frame[10] & 0x10) ? RX_PANEL_CUE_2 : 0) |
           ((frame[1] & 0x40) ? RX_PANEL_SYNC_1 : 0) |
           ((frame[5] & 0x40) ? RX_PANEL_SYNC_2 : 0) |
           ((frame[19] & 0x40) ? 0 : RX_PANEL_BEAT_FX) |
           ((frame[3] & 0x40) ? 0 : RX_PANEL_LOOP_IN_1) |
           ((frame[3] & 0x20) ? 0 : RX_PANEL_LOOP_OUT_1) |
           ((frame[7] & 0x40) ? 0 : RX_PANEL_LOOP_IN_2) |
           ((frame[7] & 0x20) ? 0 : RX_PANEL_LOOP_OUT_2) |
           ((frame[3] & 0x08) ? RX_PANEL_MASTER_TEMPO_1 : 0) |
           ((frame[7] & 0x08) ? RX_PANEL_MASTER_TEMPO_2 : 0);
}

#include "rx_panel_led.h"

int rx_panel_cue_decode(const uint8_t *frame, size_t length)
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
    return ((frame[10] & 0x20) ? 1 : 0) |
           ((frame[10] & 0x10) ? 2 : 0);
}

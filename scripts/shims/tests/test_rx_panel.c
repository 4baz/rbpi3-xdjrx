#include "../rx_panel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned crc_x25(const unsigned char *frame)
{
    unsigned crc = 0xffff;
    for (unsigned i = 0; i < 96; i++) {
        crc ^= frame[(i & ~3u) + 3 - (i & 3u)];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    return crc ^ 0xffff;
}

static void check_crc(const unsigned char *frame)
{
    assert(((unsigned)frame[0x62] | (unsigned)frame[0x63] << 8) == crc_x25(frame));
}

int main(void)
{
    struct rx_panel_state state = {{0}, 0};
    unsigned char frame[RX_PANEL_FRAME_SIZE];

    rx_panel_frame(&state, "", frame);
    check_crc(frame);
    assert(frame[0x33] == 0x80 && frame[0x43] == 0x80);
    assert(frame[0x5d] == 1);
    assert(frame[0x04] == 0 && frame[0x05] == 2);

    rx_panel_frame(&state, "B 33 80\nB 43 80\nB 5d 10\nA 54 1234\n", frame);
    check_crc(frame);
    assert(frame[0x5d] == 0x11);
    assert(frame[0x54] == 0x34 && frame[0x55] == 0x12);

    rx_panel_frame(&state, "B 33 80\nB 43 80\n", frame);
    assert(frame[0x5d] == 1 && frame[0x54] == 0);
    rx_panel_frame(&state, "B 5d 01\n", frame);
    assert((frame[0x5d] & 1) == 0);

    rx_panel_frame(&state, "b 5d 10\nB 33 80\nB 43 80\n", frame);
    assert(frame[0x5d] == 0x11);
    for (int i = 0; i < 22; i++)
        rx_panel_frame(&state, "b 5d 10\nB 33 80\nB 43 80\n", frame);
    assert(frame[0x5d] == 1);
    check_crc(frame);
    puts("RX panel frame tests passed");
    return 0;
}

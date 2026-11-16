/* XDJ-RX panel frames, as documented by xdj-rx-tools'
 * docs/rbp-and-panel-protocol.md and docs/subucom-frame-map.md. */
#define _GNU_SOURCE
#include "rx_panel.h"
#include "syscalls.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void put16(uint8_t *frame, unsigned offset, unsigned value)
{
    frame[offset] = value & 0xff;
    frame[offset + 1] = (value >> 8) & 0xff;
}

void rx_panel_frame(struct rx_panel_state *state, const char *snapshot,
                    uint8_t frame[RX_PANEL_FRAME_SIZE])
{
    static const struct { unsigned offset, value; } defaults[] = {
        {0x04, 512}, {0x0a, 511}, {0x14, 512}, {0x1a, 511},
        {0x24, 512}, {0x26, 1023}, {0x28, 512}, {0x2a, 512},
        {0x2c, 1023}, {0x2e, 512}, {0x34, 512}, {0x36, 1023},
        {0x38, 512}, {0x3a, 512}, {0x3c, 1023}, {0x3e, 512},
        {0x4a, 1023}, {0x4e, 512}
    };
    const char *line;
    unsigned crc = 0xffff;
    int pulse_used = 0;

    if (strcmp(state->snapshot, snapshot) != 0) {
        snprintf(state->snapshot, sizeof state->snapshot, "%s", snapshot);
        state->pulse_ticks = 22;
    }
    memset(frame, 0, RX_PANEL_FRAME_SIZE);
    for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
        put16(frame, defaults[i].offset, defaults[i].value);
    if (!snapshot[0])
        frame[0x33] = frame[0x43] = 0x80;

    for (line = snapshot; *line;) {
        char type;
        unsigned off, value;
        const char *next = strchr(line, '\n');
        if (sscanf(line, " %c %x %x", &type, &off, &value) == 3 &&
            off < 0x60 && value <= 0xffff) {
            int pulse = type == 'b' || type == 'a';
            int active = !pulse || (state->pulse_ticks && !pulse_used);
            if (pulse)
                pulse_used = 1;
            if (active && (type == 'B' || type == 'b') && value <= 0xff)
                frame[off] |= (uint8_t)value;
            else if (active && (type == 'A' || type == 'a') && off < 0x5f)
                put16(frame, off, value);
        }
        if (!next)
            break;
        line = next + 1;
    }
    if (state->pulse_ticks)
        state->pulse_ticks--;
    frame[0x5d] ^= 1; /* POWER is active-low. */
    for (unsigned i = 0; i < 96; i += 4) {
        for (int j = 3; j >= 0; j--) {
            crc ^= frame[i + j];
            for (int bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
        }
    }
    put16(frame, 0x62, crc ^ 0xffff);
}

static void *rx_panel_worker(void *arg)
{
    int fd = *(int *)arg;
    struct rx_panel_state state = {{0}, 0};
    uint8_t frame[RX_PANEL_FRAME_SIZE], last[RX_PANEL_FRAME_SIZE];
    int have_last = 0;

    free(arg);
    for (;;) {
        char snapshot[2048] = {0};
        char tx[256];
        int file = real_open("/tmp/subucom_ctrl", O_RDONLY, 0);
        if (file >= 0) {
            ssize_t n = real_read(file, snapshot, sizeof snapshot - 1);
            if (n < 0)
                fprintf(stderr, "rx-panel: control read: %s\n", strerror(errno));
            else
                snapshot[n] = 0;
            real_close(file);
        } else if (errno != ENOENT) {
            fprintf(stderr, "rx-panel: control open: %s\n", strerror(errno));
        }
        while (recv(fd, tx, sizeof tx, MSG_DONTWAIT) > 0) {}
        rx_panel_frame(&state, snapshot, frame);
        if (!have_last || memcmp(frame, last, sizeof frame)) {
            ssize_t n = send(fd, frame, sizeof frame, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n == sizeof frame) {
                memcpy(last, frame, sizeof frame);
                have_last = 1;
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "rx-panel: frame send: %s\n", strerror(errno));
                break;
            }
        }
        usleep(20000);
    }
    real_close(fd);
    return NULL;
}

int rx_panel_open(void)
{
    int pair[2], *worker_fd;
    pthread_t tid;
    int err;
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) < 0)
        return -1;
    worker_fd = malloc(sizeof *worker_fd);
    if (!worker_fd) {
        real_close(pair[0]);
        real_close(pair[1]);
        errno = ENOMEM;
        return -1;
    }
    *worker_fd = pair[1];
    err = pthread_create(&tid, NULL, rx_panel_worker, worker_fd);
    if (err) {
        free(worker_fd);
        real_close(pair[0]);
        real_close(pair[1]);
        errno = err;
        return -1;
    }
    pthread_detach(tid);
    fprintf(stderr, "rx-panel: emulating RX sub-ucom frame transport\n");
    return pair[0];
}

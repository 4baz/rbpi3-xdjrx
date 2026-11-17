#include "rbp_vu.c"

static int rx_profile, queries, sends, keys[4], channels[4];
int g_fader_seen[3];

int rbp_on_rx_profile(void) { return rx_profile; }
int led_query_absolute(void) { queries++; return 0; }
void klog(const char *fmt, ...) { (void)fmt; }
void send_rx_key_f(int key, int op, int channel, long value, float fvalue)
{
     (void)op;
     (void)value;
     (void)fvalue;
     if (sends < 4) {
          keys[sends] = key;
          channels[sends] = channel;
     }
     sends++;
}

int main(void)
{
     rx_profile = 1;
     mixer_defaults_tick(0);
     mixer_defaults_tick(80);
     mixer_defaults_tick(1200);
     if (sends || queries) {
          printf("RX must not receive RX3 mixer defaults or queries\n");
          return 1;
     }
     rx_profile = 0;
     mixer_defaults_tick(0);
     if (queries != 1 || sends != 3 || keys[0] != K_MASTERLVL ||
         keys[1] != K_FADER || channels[1] != 1 ||
         keys[2] != K_FADER || channels[2] != 2) {
          printf("RX3 startup defaults changed\n");
          return 1;
     }
     sends = 0;
     g_fader_seen[1] = 1;
     mixer_defaults_tick(80);
     if (sends != 2 || keys[0] != K_MASTERLVL ||
         keys[1] != K_FADER || channels[1] != 2) {
          printf("RX3 must not override a reported fader\n");
          return 1;
     }
     puts("test_mixer_defaults: passed");
     return 0;
}

// A fake libasound for the engine test: records every snd_seq_event_t the plugin sends.
#include <string.h>
#include <stddef.h>
static unsigned char g_ev[4096][28]; static int g_n = 0, g_opened = 0, g_ports = 0, g_drains = 0; static char g_client[64], g_port[64];
int snd_seq_open(void** h, const char* name, int streams, int mode) { (void)name; (void)streams; (void)mode; static int dummy; *h = &dummy; g_opened = 1; return 0; }
int snd_seq_close(void* h) { (void)h; return 0; }
int snd_seq_set_client_name(void* h, const char* n) { (void)h; strncpy(g_client, n, 63); return 0; }
int snd_seq_set_client_pool_output(void* h, size_t n) { (void)h; (void)n; return 0; }
int snd_seq_create_simple_port(void* h, const char* n, unsigned caps, unsigned type) { (void)h; strncpy(g_port, n, 63); g_ports = (int)((caps & 0x21) == 0x21 && (type & 2)); return 0; }
int snd_seq_event_output(void* h, void* ev) { (void)h; if (g_n < 4096) memcpy(g_ev[g_n++], ev, 28); return 1; }
int snd_seq_drain_output(void* h) { (void)h; ++g_drains; return 0; }
int stub_count(void) { return g_n; }
int stub_opened(void) { return g_opened; }
int stub_port_ok(void) { return g_ports; }
const char* stub_names(int which) { return which ? g_port : g_client; }
const unsigned char* stub_event(int i) { return g_ev[i]; }

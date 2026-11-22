/*
 * picogl_host_core1.c — host stand-in for the RP2040 second core.
 *
 * The picocalc image links pico_core1.c instead. These no-ops make the
 * span paint on the calling core, which is what the host gates check.
 */
#include "picogl.h"

void pico_core1_launch(void) {}
void pico_core1_reap(void) {}
void pico_core1_sev(void) {}
int  pico_core1_state(void) { return 0; }
int  pico_core1_run_flush(void) { return 0; }
unsigned pico_core1_queued(void) { return 0; }
unsigned pico_core1_refused(void) { return 0; }

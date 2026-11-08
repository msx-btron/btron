/*
 * B-BT: Standard B-Audio Driver Interface for Bluetooth A2DP
 * Conforming to B-Audio specification & NASA JPL Rules
 */

#ifndef _BTRON_B_BT_AUDIO_H_
#define _BTRON_B_BT_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* B-Audio Standard 5-Call Driver Interface */
int      b_bt_audio_open(uint32_t sample_rate, uint8_t channels);
int      b_bt_audio_write(const int16_t *samples, size_t frames);
void     b_bt_audio_close(void);
bool     b_bt_audio_is_ready(void);
uint32_t b_bt_audio_get_queued_bytes(void);
bool     b_bt_audio_is_present(void);

#endif /* _BTRON_B_BT_AUDIO_H_ */


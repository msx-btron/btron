/*
 * B-System (BTRON 3.20) — include/media/synth.h
 * Native B-System Synth Visualizer Window (HAL-9000 / Music.html Conformance)
 * Pure C99, NASA/JPL Power of Ten.
 */

#ifndef _BTRON_MEDIA_SYNTH_H_
#define _BTRON_MEDIA_SYNTH_H_

#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

WND *open_synth_window(void);
WND *open_synth_about_window(void);
void synth_notify_track_changed(void);
void synth_set_lilcu64_track(int track_num);
void synth_ui_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_MEDIA_SYNTH_H_ */

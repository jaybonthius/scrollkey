#ifndef SCROLLKEY_SCROLL_H
#define SCROLLKEY_SCROLL_H

#include <stdbool.h>
#include <stdint.h>

/* Karabiner's mouse_motion_to_scroll counter, without its HID/dispatcher layer.
 * Times are monotonic milliseconds; output values are signed wheel ticks. */
typedef struct ScrollCounter ScrollCounter;
typedef bool (*ScrollEmit)(void *context, int64_t time_ms, int horizontal, int vertical);

ScrollCounter *scroll_create(double speed_multiplier, bool momentum_scroll_enabled);
void scroll_destroy(ScrollCounter *counter);
void scroll_reset(ScrollCounter *counter);
bool scroll_input(ScrollCounter *counter, int x, int y, int64_t time_ms);
bool scroll_tick(ScrollCounter *counter, int64_t time_ms, ScrollEmit emit, void *context);
/* -1 means idle. The caller need not run a timer while the counter is idle. */
int64_t scroll_deadline(const ScrollCounter *counter);

#endif

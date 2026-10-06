#ifndef SCROLLKEY_PLATFORM_H
#define SCROLLKEY_PLATFORM_H

#include <stdbool.h>

/* Each family has either-side, left-only, and right-only bindings in that order. */
typedef enum {
    SCROLL_MOD_SHIFT, SCROLL_MOD_LEFT_SHIFT, SCROLL_MOD_RIGHT_SHIFT,
    SCROLL_MOD_CONTROL, SCROLL_MOD_LEFT_CONTROL, SCROLL_MOD_RIGHT_CONTROL,
    SCROLL_MOD_ALT, SCROLL_MOD_LEFT_ALT, SCROLL_MOD_RIGHT_ALT,
    SCROLL_MOD_META, SCROLL_MOD_LEFT_META, SCROLL_MOD_RIGHT_META
} ScrollModifier;

typedef struct {
    ScrollModifier modifier;
    double speed_multiplier;
} ScrollConfig;

int platform_run(const ScrollConfig *config);

#endif

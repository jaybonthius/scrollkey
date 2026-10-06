#ifndef SCROLLKEY_PLATFORM_H
#define SCROLLKEY_PLATFORM_H

#include <stdbool.h>

/* Each family has either-side, left-only, and right-only bindings in that order. */
typedef enum {
    SCROLL_MOD_SHIFT, SCROLL_MOD_LEFT_SHIFT, SCROLL_MOD_RIGHT_SHIFT,
    SCROLL_MOD_CONTROL, SCROLL_MOD_LEFT_CONTROL, SCROLL_MOD_RIGHT_CONTROL,
    SCROLL_MOD_ALT, SCROLL_MOD_LEFT_ALT, SCROLL_MOD_RIGHT_ALT,
    SCROLL_MOD_META, SCROLL_MOD_LEFT_META, SCROLL_MOD_RIGHT_META,
    SCROLL_MOD_NONE
} ScrollModifier;

typedef struct {
    ScrollModifier modifier;
    double speed_multiplier;
    bool diagonal;
    ScrollModifier diagonal_modifier;
} ScrollConfig;

/* Bits select physical left/right keys in Shift, Control, Alt, Meta order. */
static inline unsigned scroll_modifier_mask(ScrollModifier modifier)
{
    if ((unsigned)modifier >= SCROLL_MOD_NONE) return 0;
    unsigned side = (unsigned)modifier % 3;
    return (side == 0 ? 3u : 1u << (side - 1)) << (2 * ((unsigned)modifier / 3));
}

static inline unsigned scroll_output_modifiers(const ScrollConfig *config)
{
    return scroll_modifier_mask(config->modifier) | scroll_modifier_mask(config->diagonal_modifier);
}

int platform_run(const ScrollConfig *config);

#endif

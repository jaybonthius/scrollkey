#ifndef SCROLLKEY_SCROLL_H
#define SCROLLKEY_SCROLL_H

#include <stdbool.h>

/* DragScroll's immediate -3 * mouse-delta mapping, with fractional speed carry.
 * Quartz uses these as pixels; Windows uses high-resolution wheel units and
 * reverses the horizontal sign to match that platform's wheel convention. */
typedef struct {
    double speed_multiplier;
    double remainder_x, remainder_y;
    bool diagonal;
    unsigned axis; /* 0: unset, 1: horizontal, 2: vertical. */
} ScrollMotion;

typedef struct { int horizontal, vertical; } ScrollDelta;

bool scroll_init(ScrollMotion *motion, double speed_multiplier);
void scroll_reset(ScrollMotion *motion);
void scroll_set_diagonal(ScrollMotion *motion, bool enabled);
bool scroll_input(ScrollMotion *motion, int x, int y, ScrollDelta *output);

#endif

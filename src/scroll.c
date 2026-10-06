/* Direct scrolling follows DragScroll at 879bd0e6f4f1aae239364c5e7e49f19746729993.
 * Copyright (c) 2024 Emre Yolcu; MIT notice in THIRD_PARTY_NOTICES.
 * Optional stable single-axis filtering; no timer or software momentum. */
#include "scroll.h"

#include <limits.h>
#include <math.h>

bool scroll_init(ScrollMotion *motion, double speed_multiplier)
{
    if (!isfinite(speed_multiplier) || speed_multiplier <= 0.0)
        return false;
    *motion = (ScrollMotion){.speed_multiplier = speed_multiplier, .diagonal = true};
    return true;
}

void scroll_reset(ScrollMotion *motion)
{
    motion->remainder_x = motion->remainder_y = 0.0;
    motion->axis = 0;
}

void scroll_set_diagonal(ScrollMotion *motion, bool enabled)
{
    if (motion->diagonal == enabled) return;
    scroll_reset(motion);
    motion->diagonal = enabled;
}

bool scroll_input(ScrollMotion *motion, int x, int y, ScrollDelta *output)
{
    unsigned axis = motion->axis;
    double remainder_x = motion->remainder_x, remainder_y = motion->remainder_y;
    if (!motion->diagonal) {
        double ax = fabs((double)x), ay = fabs((double)y);
        if (x || y) {
            if (!axis) axis = ax > ay ? 1 : 2;
            else if (axis == 1 && ay >= 2.0 * ax) axis = 2;
            else if (axis == 2 && ax >= 2.0 * ay) axis = 1;
        }
        if (axis != motion->axis) remainder_x = remainder_y = 0.0;
        /* Suppressed motion is discarded, never queued for a later mode. */
        if (axis == 1) { y = 0; remainder_y = 0.0; }
        else if (axis == 2) { x = 0; remainder_x = 0.0; }
    }
    double horizontal = -3.0 * x * motion->speed_multiplier + remainder_x;
    double vertical = -3.0 * y * motion->speed_multiplier + remainder_y;
    if (!isfinite(horizontal) || !isfinite(vertical) ||
        horizontal > INT_MAX || horizontal < INT_MIN ||
        vertical > INT_MAX || vertical < INT_MIN)
        return false;
    *output = (ScrollDelta){(int)horizontal, (int)vertical};
    motion->axis = axis;
    motion->remainder_x = horizontal - output->horizontal;
    motion->remainder_y = vertical - output->vertical;
    return true;
}

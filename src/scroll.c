/* Direct scrolling follows DragScroll at 879bd0e6f4f1aae239364c5e7e49f19746729993.
 * Copyright (c) 2024 Emre Yolcu; MIT notice in THIRD_PARTY_NOTICES.
 * No timer, accumulation window, direction lock or software momentum. */
#include "scroll.h"

#include <limits.h>
#include <math.h>

bool scroll_init(ScrollMotion *motion, double speed_multiplier)
{
    if (!isfinite(speed_multiplier) || speed_multiplier <= 0.0)
        return false;
    *motion = (ScrollMotion){speed_multiplier, 0.0, 0.0};
    return true;
}

void scroll_reset(ScrollMotion *motion)
{
    motion->remainder_x = motion->remainder_y = 0.0;
}

bool scroll_input(ScrollMotion *motion, int x, int y, ScrollDelta *output)
{
    double horizontal = -3.0 * x * motion->speed_multiplier + motion->remainder_x;
    double vertical = -3.0 * y * motion->speed_multiplier + motion->remainder_y;
    if (!isfinite(horizontal) || !isfinite(vertical) ||
        horizontal > INT_MAX || horizontal < INT_MIN ||
        vertical > INT_MAX || vertical < INT_MIN)
        return false;
    *output = (ScrollDelta){(int)horizontal, (int)vertical};
    motion->remainder_x = horizontal - output->horizontal;
    motion->remainder_y = vertical - output->vertical;
    return true;
}

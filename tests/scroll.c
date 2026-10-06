#include "scroll.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    exit(1); \
} } while (0)

static void reference_motion(void)
{
    ScrollMotion motion;
    ScrollDelta output;
    CHECK(scroll_init(&motion, 1.0));
    /* Independent expectations from DragScroll's -SPEED * delta, SPEED=3.
     * Diagonals must preserve BOTH axes, with no direction lock or delay. */
    static const int cases[][4] = {
        {0, 1, 0, -3}, {0, -1, 0, 3}, {1, 0, -3, 0}, {-1, 0, 3, 0},
        {4, -7, -12, 21}, {-11, 5, 33, -15}, {0, 0, 0, 0},
        {2000, -3000, -6000, 9000}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(scroll_input(&motion, cases[i][0], cases[i][1], &output));
        CHECK(output.horizontal == cases[i][2] && output.vertical == cases[i][3]);
    }
    puts("DragScroll reference motion, both axes and immediate output passed");
}

static void fractional_speed_and_release(void)
{
    ScrollMotion motion;
    ScrollDelta output;
    CHECK(scroll_init(&motion, 0.5));
    CHECK(scroll_input(&motion, 1, -1, &output));
    CHECK(output.horizontal == -1 && output.vertical == 1);
    CHECK(scroll_input(&motion, 1, -1, &output));
    CHECK(output.horizontal == -2 && output.vertical == 2);
    CHECK(scroll_input(&motion, -1, 1, &output));
    CHECK(output.horizontal == 1 && output.vertical == -1);
    CHECK(scroll_input(&motion, -1, 1, &output));
    CHECK(output.horizontal == 2 && output.vertical == -2);
    CHECK(scroll_init(&motion, 0.125));
    int total = 0;
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(scroll_input(&motion, 0, 1, &output));
        total += output.vertical;
    }
    CHECK(total == -3);
    CHECK(scroll_init(&motion, 0.25));
    CHECK(scroll_input(&motion, 1, 0, &output));
    CHECK(output.horizontal == 0);
    scroll_reset(&motion); /* Release discards fractional carry, not speed. */
    CHECK(scroll_input(&motion, 1, 0, &output));
    CHECK(output.horizontal == 0);
    CHECK(scroll_input(&motion, 1, 0, &output));
    CHECK(output.horizontal == -1);
    for (unsigned i = 0; i < 1000; ++i) {
        CHECK(scroll_input(&motion, 0, 0, &output));
        CHECK(output.horizontal == 0 && output.vertical == 0);
    }
    puts("fractional speed, release/reset and no software momentum passed");
}

static void safe_failures(void)
{
    ScrollMotion motion;
    ScrollDelta output = {12, 34};
    CHECK(!scroll_init(&motion, 0.0));
    CHECK(!scroll_init(&motion, -1.0));
    CHECK(!scroll_init(&motion, NAN));
    CHECK(!scroll_init(&motion, INFINITY));
    CHECK(scroll_init(&motion, 1.0));
    CHECK(!scroll_input(&motion, INT_MAX, 0, &output));
    CHECK(!scroll_input(&motion, 0, INT_MIN, &output));
    CHECK(output.horizontal == 12 && output.vertical == 34);
    CHECK(motion.remainder_x == 0.0 && motion.remainder_y == 0.0);
    CHECK(scroll_input(&motion, 1, 1, &output));
    CHECK(output.horizontal == -3 && output.vertical == -3);
    CHECK(scroll_init(&motion, DBL_MAX));
    CHECK(scroll_input(&motion, 0, 0, &output));
    CHECK(!scroll_input(&motion, 1, 0, &output));
    CHECK(scroll_init(&motion, 0.125));
    CHECK(scroll_input(&motion, INT_MAX, INT_MIN, &output));
    CHECK(output.horizontal < 0 && output.vertical > 0);
    puts("invalid settings and arithmetic failures passed");
}

static void diagonal_modes(void)
{
    ScrollMotion motion;
    ScrollDelta output;
    CHECK(scroll_init(&motion, 1.0));
    scroll_set_diagonal(&motion, false);
    CHECK(scroll_input(&motion, 4, -7, &output));
    CHECK(output.horizontal == 0 && output.vertical == 21);
    /* Near-diagonal wobble must not repeatedly swap the selected axis. */
    CHECK(scroll_input(&motion, 8, -7, &output));
    CHECK(output.horizontal == 0 && output.vertical == 21);
    CHECK(scroll_input(&motion, -7, 8, &output));
    CHECK(output.horizontal == 0 && output.vertical == -24);
    CHECK(scroll_input(&motion, 0, 0, &output));
    CHECK(output.horizontal == 0 && output.vertical == 0);
    /* A deliberate turn switches immediately, without releasing the key. */
    CHECK(scroll_input(&motion, 14, -7, &output));
    CHECK(output.horizontal == -42 && output.vertical == 0);
    CHECK(scroll_input(&motion, -8, 7, &output));
    CHECK(output.horizontal == 24 && output.vertical == 0);
    scroll_set_diagonal(&motion, true);
    CHECK(scroll_input(&motion, 4, -7, &output));
    CHECK(output.horizontal == -12 && output.vertical == 21);
    scroll_set_diagonal(&motion, false);
    CHECK(scroll_input(&motion, 4, -7, &output));
    CHECK(output.horizontal == 0 && output.vertical == 21);
    scroll_reset(&motion);
    CHECK(scroll_input(&motion, 7, 4, &output));
    CHECK(output.horizontal == -21 && output.vertical == 0);
    CHECK(scroll_init(&motion, 0.125));
    scroll_set_diagonal(&motion, false);
    CHECK(scroll_input(&motion, 1, 1, &output));
    CHECK(output.horizontal == 0 && output.vertical == 0); /* Ties favor vertical. */
    scroll_set_diagonal(&motion, true); /* Drop old single-axis fractional carry. */
    CHECK(scroll_input(&motion, 1, 1, &output));
    CHECK(output.horizontal == 0 && output.vertical == 0);
    scroll_set_diagonal(&motion, true); /* Same mode must NOT discard new carry. */
    CHECK(scroll_input(&motion, 1, 1, &output));
    CHECK(output.horizontal == 0 && output.vertical == 0);
    CHECK(scroll_input(&motion, 1, 1, &output));
    CHECK(output.horizontal == -1 && output.vertical == -1);
    scroll_set_diagonal(&motion, false);
    CHECK(scroll_input(&motion, INT_MIN, 0, &output) == true);
    CHECK(output.horizontal > 0 && output.vertical == 0);
    puts("diagonal modes, stable single-axis selection, turns and mode-change carry passed");
}

int main(void)
{
    diagonal_modes();
    reference_motion();
    fractional_speed_and_release();
    safe_failures();
    return 0;
}

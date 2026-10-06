#include "scroll.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "fixtures.h"

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    exit(1); \
} } while (0)

typedef struct { const Fixture *fixture; size_t emitted; } Recording;

static bool record_fixture(void *context, int64_t time_ms, int horizontal, int vertical)
{
    Recording *recording = context;
    const Fixture *f = recording->fixture;
    if (recording->emitted >= f->expected_count) {
        fprintf(stderr, "%s: unexpected output at %lld\n", f->name, (long long)time_ms);
        exit(1);
    }
    FixtureOutput expected = f->expected[recording->emitted++];
    if (time_ms != expected.time_ms || horizontal != expected.horizontal || vertical != expected.vertical) {
        fprintf(stderr, "%s: got (%lld, %d, %d), expected (%lld, %d, %d)\n", f->name,
                (long long)time_ms, horizontal, vertical,
                (long long)expected.time_ms, expected.horizontal, expected.vertical);
        exit(1);
    }
    return true;
}

static void upstream_recordings(void)
{
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        const Fixture *f = &fixtures[i];
        Recording recording = {f, 0};
        ScrollCounter *counter = scroll_create(f->speed, f->momentum);
        CHECK(counter);
        /* Upstream enqueues the entire recording, then advances its fake clock. */
        for (size_t j = 0; j < f->input_count; ++j) {
            FixtureMotion motion = f->input[j];
            CHECK(scroll_input(counter, motion.x, motion.y, motion.time_ms));
        }
        int64_t end = f->input[f->input_count - 1].time_ms + 1000;
        for (int64_t now = 0; now <= end; now += 10)
            CHECK(scroll_tick(counter, now, record_fixture, &recording));
        CHECK(recording.emitted == f->expected_count);
        scroll_destroy(counter);
        printf("upstream %s passed\n", f->name);
    }
}

typedef struct { FixtureOutput events[32]; size_t count; } Output;

static bool capture(void *context, int64_t time_ms, int horizontal, int vertical)
{
    Output *out = context;
    CHECK(out->count < sizeof(out->events) / sizeof(out->events[0]));
    out->events[out->count++] = (FixtureOutput){time_ms, horizontal, vertical};
    return true;
}

static void advance(ScrollCounter *counter, int64_t begin, int64_t end, Output *out)
{
    for (int64_t time = begin; time <= end; time += 10)
        CHECK(scroll_tick(counter, time, capture, out));
}

static void half_speed_and_release(void)
{
    Output out = {0};
    ScrollCounter *counter = scroll_create(0.5, false);
    CHECK(counter);
    CHECK(scroll_input(counter, 512, 0, 0));
    advance(counter, 0, 1000, &out);
    CHECK(out.count == 1);
    CHECK(out.events[0].time_ms == 120 && out.events[0].horizontal == 2 &&
          out.events[0].vertical == 0);
    scroll_destroy(counter);

    counter = scroll_create(1.0, true);
    CHECK(counter);
    CHECK(scroll_input(counter, 512, 0, 0));
    out.count = 0;
    advance(counter, 0, 120, &out);
    CHECK(out.count == 1 && out.events[0].horizontal == 1);
    scroll_reset(counter); /* Modifier released during momentum. */
    CHECK(scroll_deadline(counter) == -1);
    advance(counter, 130, 2000, &out);
    CHECK(out.count == 1);
    CHECK(scroll_input(counter, 0, 1, 2000));
    advance(counter, 2000, 3000, &out);
    CHECK(out.count == 2 && out.events[1].time_ms == 2120 &&
          out.events[1].horizontal == 0 && out.events[1].vertical == -1);

    CHECK(scroll_input(counter, 100, 0, 4000));
    scroll_reset(counter); /* Modifier released before the delayed first scroll. */
    advance(counter, 4000, 5000, &out);
    CHECK(out.count == 2);
    scroll_destroy(counter);
    puts("half-speed, release, and reactivation passed");
}

static bool reject_output(void *context, int64_t time_ms, int horizontal, int vertical)
{
    (void)context; (void)time_ms; (void)horizontal; (void)vertical;
    return false;
}

static void safe_failures(void)
{
    CHECK(!scroll_create(0.0, true));
    CHECK(!scroll_create(-1.0, true));
    CHECK(!scroll_create(NAN, true));
    CHECK(!scroll_create(INFINITY, true));
    ScrollCounter *counter = scroll_create(1.0, true);
    CHECK(counter);
    CHECK(!scroll_input(counter, INT_MAX, 0, 0));
    CHECK(!scroll_input(counter, 0, INT_MIN, 0));
    CHECK(!scroll_input(counter, 0, 0, -1));
    CHECK(!scroll_input(counter, 0, 0, INT64_MAX));
    CHECK(scroll_input(counter, 0, 1, 0));
    CHECK(!scroll_tick(counter, 120, reject_output, NULL));
    CHECK(scroll_deadline(counter) == -1);
    scroll_destroy(counter);

    counter = scroll_create(1e300, true);
    CHECK(counter);
    CHECK(scroll_input(counter, 1, 0, 0));
    CHECK(!scroll_tick(counter, 120, capture, NULL));
    CHECK(scroll_deadline(counter) == -1);
    scroll_destroy(counter);
    puts("invalid values and safe failures passed");
}

int main(void)
{
    upstream_recordings();
    half_speed_and_release();
    safe_failures();
    return 0;
}

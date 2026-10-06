/* Port of Karabiner-Elements mouse_motion_to_scroll/counter.hpp and
 * counter_chunk_value.hpp at ec2fea8940f7254a748c052a890ee9db7113d9b1.
 * Upstream is dedicated to the public domain; see LICENSE and docs/port.md.
 * Keep its integer truncation, negative rounding, timing, and direction lock. */
#include "scroll.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define INTERVAL_MS 20
#define RECENT_MS 100
#define THRESHOLD 128
#define DIRECTION_WINDOW_MS 400
/* Reject pathological input instead of overflowing upstream's int counters. */
#define LIMIT (INT_MAX / 4)

typedef struct { int x, y; int64_t time_ms; } Motion;
typedef struct { int x, y; int64_t time_ms; } Chunk;
typedef struct { int plus, minus, absolute, last_sign; } ChunkValue;
typedef enum { NONE, HORIZONTAL, VERTICAL } Direction;

typedef struct {
    size_t motion_count, chunk_count;
    Direction direction;
    int total_x, total_y, momentum_x, momentum_y, momentum_minus;
    unsigned momentum_count, momentum_wait;
    int64_t next_tick, last_scroll;
    bool running, has_last_scroll;
} State;

struct ScrollCounter {
    double speed;
    bool momentum;
    Motion *motions;
    Chunk *chunks;
    size_t motion_capacity, chunk_capacity;
    State state;
};

static void *reserve(void *buffer, size_t *capacity, size_t needed, size_t element_size)
{
    if (needed <= *capacity)
        return buffer;
    size_t next = *capacity ? *capacity : 64;
    while (next < needed) {
        if (next > SIZE_MAX / 2)
            return NULL;
        next *= 2;
    }
    if (next > SIZE_MAX / element_size)
        return NULL;
    void *grown = realloc(buffer, next * element_size);
    if (grown)
        *capacity = next;
    return grown;
}

static int sign(int value) { return (value > 0) - (value < 0); }

static bool add(int *target, int value)
{
    int64_t sum = (int64_t)*target + value;
    if (sum > LIMIT || sum < -LIMIT)
        return false;
    *target = (int)sum;
    return true;
}

static bool chunk_add(ChunkValue *chunk, int value)
{
    if (value > 0) {
        if (!add(&chunk->plus, value))
            return false;
        chunk->last_sign = 1;
    } else if (value < 0) {
        if (!add(&chunk->minus, value))
            return false;
        chunk->last_sign = -1;
    }
    return add(&chunk->absolute, abs(value));
}

static int chunk_value(const ChunkValue *chunk)
{
    int plus = chunk->plus, minus = -chunk->minus;
    if (plus > minus)
        return chunk->plus;
    if (plus < minus)
        return chunk->minus;
    if (chunk->last_sign > 0)
        return chunk->plus;
    if (chunk->last_sign < 0)
        return chunk->minus;
    return 0;
}

ScrollCounter *scroll_create(double speed_multiplier, bool momentum_scroll_enabled)
{
    if (!isfinite(speed_multiplier) || speed_multiplier <= 0.0)
        return NULL;
    ScrollCounter *counter = calloc(1, sizeof(*counter));
    if (counter) {
        counter->speed = speed_multiplier;
        counter->momentum = momentum_scroll_enabled;
    }
    return counter;
}

void scroll_destroy(ScrollCounter *counter)
{
    if (counter) {
        free(counter->motions);
        free(counter->chunks);
        free(counter);
    }
}

void scroll_reset(ScrollCounter *counter)
{
    memset(&counter->state, 0, sizeof(counter->state));
}

int64_t scroll_deadline(const ScrollCounter *counter)
{
    return counter->state.running ? counter->state.next_tick : -1;
}

bool scroll_input(ScrollCounter *counter, int x, int y, int64_t time_ms)
{
    if (x > LIMIT || x < -LIMIT || y > LIMIT || y < -LIMIT ||
        time_ms < 0 || time_ms > INT64_MAX - INTERVAL_MS)
        return false;
    State *s = &counter->state;
    if (s->motion_count && time_ms < counter->motions[s->motion_count - 1].time_ms)
        return false;
    Motion *motions = reserve(counter->motions, &counter->motion_capacity,
                              s->motion_count + 1, sizeof(Motion));
    if (!motions)
        return false;
    counter->motions = motions;
    counter->motions[s->motion_count++] = (Motion){x, y, time_ms};
    if (!s->running) {
        s->running = true;
        s->next_tick = time_ms + INTERVAL_MS;
    }
    return true;
}

/* -1: failure, 0: no pending entries, 1: keep the upstream timer running. */
static int process_entries(ScrollCounter *counter, int64_t now)
{
    State *s = &counter->state;
    if (!s->motion_count)
        return 0;
    int64_t front = counter->motions[0].time_ms;
    if (now - front <= RECENT_MS)
        return 1;

    size_t retained = 0;
    for (size_t i = 0; i < s->chunk_count; ++i)
        if (front - counter->chunks[i].time_ms <= DIRECTION_WINDOW_MS)
            counter->chunks[retained++] = counter->chunks[i];
    s->chunk_count = retained;
    bool initial = s->chunk_count == 0;
    if (initial)
        s->direction = NONE;

    ChunkValue chunk_x = {0}, chunk_y = {0};
    size_t consumed = 0;
    while (consumed < s->motion_count &&
           counter->motions[consumed].time_ms - front <= RECENT_MS) {
        Motion motion = counter->motions[consumed++];
        if (!chunk_add(&chunk_x, motion.x) || !chunk_add(&chunk_y, motion.y))
            return -1;
    }
    s->motion_count -= consumed;
    memmove(counter->motions, counter->motions + consumed, s->motion_count * sizeof(Motion));
    int x = chunk_value(&chunk_x), y = chunk_value(&chunk_y);
    Chunk *chunks = reserve(counter->chunks, &counter->chunk_capacity,
                            s->chunk_count + 1, sizeof(Chunk));
    if (!chunks)
        return -1;
    counter->chunks = chunks;
    counter->chunks[s->chunk_count++] = (Chunk){x, y, front};

    int64_t recent_x = 0, recent_y = 0;
    for (size_t i = 0; i < s->chunk_count; ++i) {
        recent_x += abs(counter->chunks[i].x);
        recent_y += abs(counter->chunks[i].y);
    }
    if ((s->direction == HORIZONTAL && recent_y > recent_x) ||
        (s->direction == VERTICAL && recent_x > recent_y))
        s->direction = NONE;
    if (s->direction == NONE) {
        s->direction = chunk_x.absolute > chunk_y.absolute ? HORIZONTAL : VERTICAL;
        s->total_x = s->total_y = s->momentum_x = s->momentum_y = 0;
        s->momentum_minus = THRESHOLD;
    }
    if (s->direction == HORIZONTAL)
        y = 0;
    else
        x = 0;
    if (x != 0 && sign(s->total_x) != sign(x)) {
        s->total_x = s->momentum_x = 0;
        s->momentum_minus = THRESHOLD;
        initial = true;
    }
    if (y != 0 && sign(s->total_y) != sign(y)) {
        s->total_y = s->momentum_y = 0;
        s->momentum_minus = THRESHOLD;
        initial = true;
    }

    double scaled_x = x * counter->speed, scaled_y = y * counter->speed;
    if (!isfinite(scaled_x) || !isfinite(scaled_y) ||
        fabs(scaled_x) > LIMIT || fabs(scaled_y) > LIMIT)
        return -1;
    /* Compound int *= double in upstream truncates toward zero. */
    x = (int)scaled_x;
    y = (int)scaled_y;
    if (!add(&s->total_x, x) || !add(&s->total_y, y))
        return -1;
    if (s->total_x == 0 && s->total_y == 0)
        return 1;
    if (initial) {
        if (s->total_x > 0 && s->total_x < THRESHOLD) s->total_x = THRESHOLD;
        if (s->total_x < 0 && s->total_x > -THRESHOLD) s->total_x = -THRESHOLD;
        if (s->total_y > 0 && s->total_y < THRESHOLD) s->total_y = THRESHOLD;
        if (s->total_y < 0 && s->total_y > -THRESHOLD) s->total_y = -THRESHOLD;
    }
    int largest = abs(s->total_x) > abs(s->total_y) ? abs(s->total_x) : abs(s->total_y);
    double value = (double)largest / THRESHOLD;
    if (value > 10.0) value = 10.0;
    double decay = THRESHOLD / pow(value, 4.0);
    /* Values beyond int range only arise at extremely small multipliers. */
    int minus = decay > LIMIT ? LIMIT : (int)decay;
    if (minus <= 0) minus = 1;
    if (minus < s->momentum_minus) s->momentum_minus = minus;
    s->has_last_scroll = false;
    s->momentum_count = s->momentum_wait = 0;
    return 1;
}

static int round_up(double value)
{
    if (value > 0.0) return (int)ceil(value);
    if (value < 0.0) return (int)ceil(value) - 1;
    return 0;
}

static int convert(int *value, bool momentum)
{
    int result = *value / THRESHOLD;
    if (momentum) {
        if (result > 1) result = 1;
        if (result < -1) result = -1;
    }
    *value -= result * THRESHOLD;
    return result;
}

static void reduce(int *value, int amount)
{
    if (*value > 0) *value -= *value < amount ? *value : amount;
    if (*value < 0) *value += -*value < amount ? -*value : amount;
}

static int process_scroll(ScrollCounter *counter, int64_t now, ScrollEmit emit, void *context)
{
    State *s = &counter->state;
    if (s->momentum_wait > 0) {
        --s->momentum_wait;
        return 1;
    }
    ++s->momentum_count;
    if (s->momentum_count == 0)
        return 0;
    if (s->has_last_scroll && now - s->last_scroll > RECENT_MS)
        return 0;
    double scale = 1.0 / s->momentum_count;
    if (!counter->momentum && s->momentum_count > 1)
        scale = 0.0;
    if (!add(&s->momentum_x, round_up(s->total_x * scale)) ||
        !add(&s->momentum_y, round_up(s->total_y * scale)))
        return -1;
    int x = convert(&s->momentum_x, counter->momentum);
    int y = convert(&s->momentum_y, counter->momentum);
    if (x || y) {
        if (!emit(context, now, x, -y))
            return -1;
        s->has_last_scroll = true;
        s->last_scroll = now;
    }
    reduce(&s->total_x, s->momentum_minus);
    reduce(&s->total_y, s->momentum_minus);
    if (s->total_x == 0 && s->total_y == 0)
        return 0;
    if (counter->momentum)
        s->momentum_wait = s->momentum_count < 10 ? s->momentum_count : 10;
    return 1;
}

bool scroll_tick(ScrollCounter *counter, int64_t time_ms, ScrollEmit emit, void *context)
{
    State *s = &counter->state;
    if (!s->running || time_ms < s->next_tick)
        return true;
    if (!emit || time_ms > INT64_MAX - INTERVAL_MS) {
        scroll_reset(counter);
        return false;
    }
    int entries = process_entries(counter, time_ms);
    int scrolling = entries < 0 ? -1 : process_scroll(counter, time_ms, emit, context);
    if (entries < 0 || scrolling < 0) {
        scroll_reset(counter);
        return false;
    }
    s->running = entries > 0 || scrolling > 0;
    s->next_tick = time_ms + INTERVAL_MS;
    return true;
}

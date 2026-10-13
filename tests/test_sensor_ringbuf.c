/*
 * SPDX-License-Identifier: Apache-2.0
 * Unit Test: test_sensor_ringbuf.c
 * Validates sensor FIFO queue integrity, boundary handling, and overrun protection.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef ZEPHYR_BUILD
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include "sensor_manager.h"
#else
/* Standalone host test harnesses (GCC/Clang) */
#include <stdio.h>
#include <assert.h>

#define zassert_true(cond, msg)  assert((cond) && msg)
#define zassert_equal(a, b, msg) assert(((a) == (b)) && msg)
#endif

/* Local Ring Buffer implementation under test */
#define TEST_RINGBUF_CAPACITY 8

typedef struct {
    uint32_t timestamp;
    int32_t  temperature;
    uint32_t pressure;
} test_sample_t;

typedef struct {
    test_sample_t buffer[TEST_RINGBUF_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    uint32_t overruns;
} sensor_ringbuf_t;

static void ringbuf_init(sensor_ringbuf_t *rb)
{
    memset(rb, 0, sizeof(*rb));
}

static bool ringbuf_push(sensor_ringbuf_t *rb, const test_sample_t *item)
{
    if (rb->count >= TEST_RINGBUF_CAPACITY) {
        rb->overruns++;
        /* Drop oldest entry to make room */
        rb->tail = (rb->tail + 1) % TEST_RINGBUF_CAPACITY;
        rb->count--;
    }

    rb->buffer[rb->head] = *item;
    rb->head = (rb->head + 1) % TEST_RINGBUF_CAPACITY;
    rb->count++;
    return true;
}

static bool ringbuf_pop(sensor_ringbuf_t *rb, test_sample_t *out_item)
{
    if (rb->count == 0) {
        return false;
    }

    *out_item = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % TEST_RINGBUF_CAPACITY;
    rb->count--;
    return true;
}

/* ==================== Unit Test Cases ==================== */

static void test_ringbuf_empty_initial_state(void)
{
    sensor_ringbuf_t rb;
    ringbuf_init(&rb);

    zassert_equal(rb.count, 0, "Initial count should be 0");
    zassert_equal(rb.overruns, 0, "Initial overruns should be 0");

    test_sample_t dummy;
    bool popped = ringbuf_pop(&rb, &dummy);
    zassert_true(!popped, "Pop from empty ring buffer should return false");
}

static void test_ringbuf_fifo_ordering(void)
{
    sensor_ringbuf_t rb;
    ringbuf_init(&rb);

    test_sample_t s1 = { .timestamp = 100, .temperature = 2500, .pressure = 101325 };
    test_sample_t s2 = { .timestamp = 200, .temperature = 2550, .pressure = 101330 };
    test_sample_t s3 = { .timestamp = 300, .temperature = 2600, .pressure = 101340 };

    ringbuf_push(&rb, &s1);
    ringbuf_push(&rb, &s2);
    ringbuf_push(&rb, &s3);

    zassert_equal(rb.count, 3, "Count should be 3");

    test_sample_t out;
    zassert_true(ringbuf_pop(&rb, &out), "Pop 1 should succeed");
    zassert_equal(out.timestamp, 100, "FIFO order mismatch on pop 1");

    zassert_true(ringbuf_pop(&rb, &out), "Pop 2 should succeed");
    zassert_equal(out.timestamp, 200, "FIFO order mismatch on pop 2");

    zassert_true(ringbuf_pop(&rb, &out), "Pop 3 should succeed");
    zassert_equal(out.timestamp, 300, "FIFO order mismatch on pop 3");

    zassert_equal(rb.count, 0, "Ringbuf should now be empty");
}

static void test_ringbuf_overrun_drop_oldest(void)
{
    sensor_ringbuf_t rb;
    ringbuf_init(&rb);

    /* Push capacity + 3 items */
    for (uint32_t i = 1; i <= TEST_RINGBUF_CAPACITY + 3; i++) {
        test_sample_t item = { .timestamp = i, .temperature = (int32_t)i * 10, .pressure = 100000 };
        ringbuf_push(&rb, &item);
    }

    zassert_equal(rb.count, TEST_RINGBUF_CAPACITY, "Count should be capped at capacity");
    zassert_equal(rb.overruns, 3, "Overrun count should record 3 dropped frames");

    /* Verify that the 3 oldest items (1, 2, 3) were dropped and item 4 is now at tail */
    test_sample_t out;
    zassert_true(ringbuf_pop(&rb, &out), "Pop should succeed");
    zassert_equal(out.timestamp, 4, "Oldest surviving item should have timestamp=4");
}

#ifdef ZEPHYR_BUILD
ZTEST(sensor_ringbuf_suite, test_empty) { test_ringbuf_empty_initial_state(); }
ZTEST(sensor_ringbuf_suite, test_fifo)  { test_ringbuf_fifo_ordering(); }
ZTEST(sensor_ringbuf_suite, test_overrun){ test_ringbuf_overrun_drop_oldest(); }
ZTEST_SUITE(sensor_ringbuf_suite, NULL, NULL, NULL, NULL, NULL);
#else
int main(void)
{
    printf("Running sensor_ringbuf unit tests...\n");
    test_ringbuf_empty_initial_state();
    printf("  [PASS] test_ringbuf_empty_initial_state\n");
    test_ringbuf_fifo_ordering();
    printf("  [PASS] test_ringbuf_fifo_ordering\n");
    test_ringbuf_overrun_drop_oldest();
    printf("  [PASS] test_ringbuf_overrun_drop_oldest\n");
    printf("All 3 unit tests passed successfully!\n");
    return 0;
}
#endif

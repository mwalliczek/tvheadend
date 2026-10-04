#include <check.h>

#include "tvheadend.h"
#include "sdr_fifo.h"
#include <pthread.h>

static void fill(uint8_t *buf, int len, int start) {
    for (int i = 0; i < len; i++)
        buf[i] = (uint8_t)(start + i);
}

START_TEST(writeReadWrapTest) {
    CircularBuffer cb;
    uint8_t buf[8];
    cbInit(&cb, 8);
    ck_assert(cbIsEmpty(&cb));

    fill(buf, 6, 0);
    cbWrite(&cb, buf, 6);
    ck_assert_int_eq(cb.count, 6);
    ck_assert_int_eq(cbReadDouble(&cb)[0], 0);
    ck_assert_int_eq(cbReadDouble(&cb)[1], 3);

    /* wraps around the end of the buffer */
    fill(buf, 4, 6);
    cbWrite(&cb, buf, 4);
    ck_assert_int_eq(cb.count, 6);
    for (int i = 4; i < 10; i += 2) {
        uint8_t *p = cbReadDouble(&cb);
        ck_assert_int_eq(p[0], i);
        ck_assert_int_eq(p[1], i + 1);
    }
    ck_assert(cbIsEmpty(&cb));
    cbFree(&cb);
} END_TEST

START_TEST(exactlyFullTest) {
    CircularBuffer cb;
    uint8_t buf[8];
    cbInit(&cb, 8);
    fill(buf, 4, 0);
    cbWrite(&cb, buf, 4);
    fill(buf, 4, 4);
    cbWrite(&cb, buf, 4);
    /* filling the buffer completely must not drop anything */
    ck_assert(cbIsFull(&cb));
    ck_assert_int_eq(cb.start, 0);
    ck_assert_int_eq(cbReadDouble(&cb)[0], 0);
    cbFree(&cb);
} END_TEST

START_TEST(overflowDropsNewDataTest) {
    CircularBuffer cb;
    uint8_t buf[12];
    cbInit(&cb, 8);
    fill(buf, 6, 0);
    cbWrite(&cb, buf, 6);
    fill(buf, 4, 6);
    cbWrite(&cb, buf, 4);           /* only bytes 6 and 7 still fit */
    ck_assert(cbIsFull(&cb));
    for (int i = 0; i < 8; i += 2)
        ck_assert_int_eq(cbReadDouble(&cb)[0], i);
    ck_assert(cbIsEmpty(&cb));

    /* a single write larger than the buffer is cut */
    fill(buf, 12, 100);
    cbWrite(&cb, buf, 12);
    ck_assert(cbIsFull(&cb));
    for (int i = 100; i < 108; i += 2)
        ck_assert_int_eq(cbReadDouble(&cb)[0], i);
    cbFree(&cb);
} END_TEST

static void *producer(void *arg) {
    CircularBuffer *cb = arg;
    uint8_t buf[64];
    uint32_t value = 0;
    while (value < 400000) {
        if (cb->size - cbCount(cb) < sizeof(buf))
            continue;
        for (unsigned i = 0; i < sizeof(buf); i++)
            buf[i] = (uint8_t)(value++ / 2);
        cbWrite(cb, buf, sizeof(buf));
    }
    return NULL;
}

START_TEST(concurrentProducerConsumerTest) {
    CircularBuffer cb;
    pthread_t thread;
    uint32_t expected = 0;
    cbInit(&cb, 1024);
    pthread_create(&thread, NULL, producer, &cb);
    while (expected < 200000) {
        uint8_t *p;
        if (cbCount(&cb) < 2)
            continue;
        p = cbReadDouble(&cb);
        ck_assert_int_eq(p[0], (uint8_t)expected);
        ck_assert_int_eq(p[1], (uint8_t)expected);
        expected++;
    }
    pthread_join(thread, NULL);
    ck_assert(cbIsEmpty(&cb));
    cbFree(&cb);
} END_TEST

static Suite *sdr_fifo_suite(void) {
    Suite *s = suite_create("sdr_fifo");
    TCase *tc_core = tcase_create("Core");
    tcase_set_timeout(tc_core, 60);
    tcase_add_test(tc_core, writeReadWrapTest);
    tcase_add_test(tc_core, exactlyFullTest);
    tcase_add_test(tc_core, overflowDropsNewDataTest);
    tcase_add_test(tc_core, concurrentProducerConsumerTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(sdr_fifo_suite());

    srunner_set_xml(sr, "sdr_fifoTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

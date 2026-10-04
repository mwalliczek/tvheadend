#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dab_scan.h"

static int run(dab_scan_state_t *st, int seconds, int synced, int complete, int incomplete) {
    dab_scan_result_t r = DAB_SCAN_CONTINUE;
    for (int i = 0; i < seconds && r == DAB_SCAN_CONTINUE; i++)
        r = dab_scan_tick(st, synced, complete, incomplete);
    return r;
}

START_TEST(emptyChannelFailsFastTest) {
    dab_scan_state_t st;
    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(run(&st, DAB_SCAN_NO_SIGNAL - 1, 0, 0, 0), DAB_SCAN_CONTINUE);
    ck_assert_int_eq(dab_scan_tick(&st, 0, 0, 0), DAB_SCAN_NO_DATA);
    ck_assert_int_eq(st.ticks, DAB_SCAN_NO_SIGNAL);
} END_TEST

START_TEST(completeWhenStableTest) {
    dab_scan_state_t st;
    memset(&st, 0, sizeof(st));
    /* sync after 2 s, services trickle in, then nothing changes */
    run(&st, 2, 0, 0, 0);
    ck_assert_int_eq(dab_scan_tick(&st, 1, 3, 2), DAB_SCAN_CONTINUE);
    ck_assert_int_eq(dab_scan_tick(&st, 1, 8, 1), DAB_SCAN_CONTINUE);
    ck_assert_int_eq(dab_scan_tick(&st, 1, 10, 0), DAB_SCAN_CONTINUE);
    ck_assert_int_eq(run(&st, DAB_SCAN_STABLE - 1, 1, 10, 0), DAB_SCAN_CONTINUE);
    ck_assert_int_eq(dab_scan_tick(&st, 1, 10, 0), DAB_SCAN_COMPLETE);
    ck_assert_int_lt(st.ticks, 15);
} END_TEST

START_TEST(partialAtTimeoutTest) {
    dab_scan_state_t st;
    memset(&st, 0, sizeof(st));
    /* one service never gets its sub-channel */
    ck_assert_int_eq(run(&st, 100, 1, 5, 1), DAB_SCAN_PARTIAL);
    ck_assert_int_eq(st.ticks, DAB_SCAN_MAX);
} END_TEST

START_TEST(syncedWithoutAudioTest) {
    dab_scan_state_t st;
    memset(&st, 0, sizeof(st));
    /* FIC received, but no audio service (data only ensemble) */
    ck_assert_int_eq(run(&st, 100, 1, 0, 0), DAB_SCAN_NO_DATA);
    ck_assert_int_eq(st.ticks, DAB_SCAN_MAX);
} END_TEST

START_TEST(band3Test) {
    ck_assert_int_eq(dab_band3_channel_count, 38);
    ck_assert_str_eq(dab_band3_channel_name(174928000), "5A");
    ck_assert_str_eq(dab_band3_channel_name(222064000), "11D");
    ck_assert_str_eq(dab_band3_channel_name(239200000), "13F");
    ck_assert_ptr_eq(dab_band3_channel_name(222000000), NULL);
    /* 1.712 MHz raster within a block, ascending */
    for (int i = 1; i < dab_band3_channel_count; i++)
        ck_assert_int_gt(dab_band3_channels[i].freq, dab_band3_channels[i - 1].freq);
} END_TEST

static Suite *dab_scan_suite(void) {
    Suite *s = suite_create("dab_scan");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, emptyChannelFailsFastTest);
    tcase_add_test(tc_core, completeWhenStableTest);
    tcase_add_test(tc_core, partialAtTimeoutTest);
    tcase_add_test(tc_core, syncedWithoutAudioTest);
    tcase_add_test(tc_core, band3Test);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(dab_scan_suite());

    srunner_set_xml(sr, "dab_scanTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

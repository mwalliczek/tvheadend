#include <check.h>

#include "firecheck.h"

START_TEST(firecheckZeroTest) {
    uint8_t test[11] = {0};
    firecheck_init();
    ck_assert_int_eq(firecode_check(test), 1);
} END_TEST

START_TEST(firecheckDetectsErrorTest) {
    firecheck_init();
    /* the fire code protects bytes 2..10, any single bit flip must be detected */
    for (int byte = 0; byte < 11; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t test[11] = {0};
            test[byte] ^= 1 << bit;
            ck_assert_msg(firecode_check(test) == 0, "bit %d of byte %d undetected", bit, byte);
        }
    }
} END_TEST

START_TEST(firecheckRealFrameTest) {
    /* start of the superframe found in the mp4processor test data (EEP 3-A, 72 kbit/s) */
    uint8_t *input = calloc(1728, sizeof(uint8_t));
    uint8_t bytes[11];
    FILE *pFile = fopen("input/dab/mp4in2", "rb");
    ck_assert_ptr_ne(pFile, NULL);
    ck_assert_int_eq(fread(input, 1, 1728, pFile), 1728);
    fclose(pFile);
    for (int i = 0; i < 11; i++) {
        bytes[i] = 0;
        for (int j = 0; j < 8; j++)
            bytes[i] = (bytes[i] << 1) | (input[i * 8 + j] & 1);
    }
    firecheck_init();
    ck_assert_int_eq(firecode_check(bytes), 1);
    bytes[5] ^= 0x10;
    ck_assert_int_eq(firecode_check(bytes), 0);
    free(input);
} END_TEST

static Suite *firecheck_suite(void) {
    Suite *s = suite_create("firecheck");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, firecheckZeroTest);
    tcase_add_test(tc_core, firecheckDetectsErrorTest);
    tcase_add_test(tc_core, firecheckRealFrameTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(firecheck_suite());

    srunner_set_xml(sr, "firecheckTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

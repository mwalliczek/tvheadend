#include <check.h>

#include "../dab.h"
#include "tvheadend.h"
#include "viterbi-768.h"

START_TEST(viterbiDecodeTest) {
    int16_t *input = calloc(3072 + 24, sizeof(int16_t));
    uint8_t *output = calloc(768, sizeof(uint8_t));
    uint8_t *expected = calloc(768, sizeof(uint8_t));
    struct v vp;
    FILE *pFile;

    initConstViterbi768();
    initViterbi768(&vp, 768);

    pFile = fopen("input/dab/rtlsdr/viterbi_768/input", "rb");
    ck_assert_ptr_ne(pFile, NULL);
    ck_assert_int_eq(fread(input, 2, 3072 + 24, pFile), 3072 + 24);
    fclose(pFile);

    pFile = fopen("input/dab/rtlsdr/viterbi_768/output", "rb");
    ck_assert_ptr_ne(pFile, NULL);
    ck_assert_int_eq(fread(expected, 1, 768, pFile), 768);
    fclose(pFile);

    deconvolve(&vp, input, output);
    ck_assert_int_eq(memcmp(output, expected, 768), 0);

    /* decoding is stateless between frames: a second run gives the same result */
    memset(output, 0, 768);
    deconvolve(&vp, input, output);
    ck_assert_int_eq(memcmp(output, expected, 768), 0);

    destroyViterbi768(&vp);
    free(input);
    free(output);
    free(expected);
} END_TEST

Suite *viterbi_suite(void) {
    Suite *s = suite_create("viterbi");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, viterbiDecodeTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(viterbi_suite());

    srunner_set_xml(sr, "viterbiTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

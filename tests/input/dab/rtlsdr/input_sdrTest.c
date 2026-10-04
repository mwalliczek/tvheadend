#include <check.h>

#include "rtlsdr_private.h"

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
}

static FILE *pFile;

int readFromDevice(rtlsdr_frontend_t *lfe) {
    uint8_t input[1024];
    if (pFile == NULL || fread(input, 1, 1024, pFile) < 1024)
        return 0;
    cbWrite(&(lfe->sdr.fifo), input, 1024);
    return 1;
}

static rtlsdr_frontend_t *create_frontend(void) {
    rtlsdr_frontend_t *lfe = calloc(1, sizeof(rtlsdr_frontend_t));
    lfe->sdr.mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    sdr_init(&lfe->sdr);
    return lfe;
}

static void destroy_frontend(rtlsdr_frontend_t *lfe) {
    sdr_destroy(&lfe->sdr);
    free(lfe->sdr.mmi);
    free(lfe);
}

START_TEST(getSamplesTest) {
    float _Complex v[512];
    float _Complex orig[512];
    FILE *pOut;
    rtlsdr_frontend_t *lfe;

    sdr_init_const();
    pFile = fopen("input/dab/rtlsdr/input_sdrTest/rtlsdr_raw", "rb");
    ck_assert_ptr_ne(pFile, NULL);
    lfe = create_frontend();

    ck_assert_int_eq(getSamples(lfe, v, 512, 0), 512);

    pOut = fopen("input/dab/rtlsdr/input_sdrTest/rtlsdr_samples", "rb");
    ck_assert_ptr_ne(pOut, NULL);
    ck_assert_int_eq(fread(orig, sizeof(float _Complex), 512, pOut), 512);
    fclose(pOut);

    ck_assert_int_eq(memcmp(v, orig, 512 * sizeof(float _Complex)), 0);
    ck_assert(lfe->sdr.sLevel > 0);

    /* no more data: getSamples reports the number of samples it got */
    ck_assert_int_eq(getSamples(lfe, v, 512, 0), 0);

    destroy_frontend(lfe);
    fclose(pFile);
    pFile = NULL;
} END_TEST

START_TEST(getSampleFrequencyShiftTest) {
    float _Complex v;
    float abs;
    rtlsdr_frontend_t *lfe;

    sdr_init_const();
    pFile = fopen("input/dab/rtlsdr/input_sdrTest/rtlsdr_raw", "rb");
    ck_assert_ptr_ne(pFile, NULL);
    lfe = create_frontend();

    /* the oscillator only rotates the sample, the magnitude stays the same */
    ck_assert_int_eq(getSample(lfe, &v, &abs, 0), 1);
    float mag0 = cabsf(v);
    ck_assert(fabsf(abs - jan_abs(v)) < 1e-6);
    lfe->sdr.fifo.start -= 2;
    lfe->sdr.fifo.count += 2;
    ck_assert_int_eq(getSample(lfe, &v, &abs, INPUT_RATE / 4), 1);
    ck_assert(fabsf(cabsf(v) - mag0) < 1e-5);
    ck_assert_int_eq(lfe->sdr.localPhase, INPUT_RATE - INPUT_RATE / 4);

    destroy_frontend(lfe);
    fclose(pFile);
    pFile = NULL;
} END_TEST

static Suite *input_sdr_suite(void) {
    Suite *s = suite_create("input_sdr");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, getSamplesTest);
    tcase_add_test(tc_core, getSampleFrequencyShiftTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(input_sdr_suite());

    srunner_set_xml(sr, "input_sdrTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

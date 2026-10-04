#include <check.h>

#include "rtlsdr_private.h"

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
}

static FILE *pFile;
static long synthetic;      /* remaining bytes of a constant test signal */

int readFromDevice(rtlsdr_frontend_t *lfe) {
    uint8_t input[1024];
    if (synthetic > 0) {
        for (int i = 0; i < 1024; i += 2) {
            input[i] = 128 + 72;    /* I = 72 / 128 */
            input[i + 1] = 128;     /* Q = 0 */
        }
        synthetic -= 1024;
        cbWrite(&(lfe->sdr.fifo), input, 1024);
        return 1;
    }
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

/* the frequency correction must stay exact over a long time */
START_TEST(oscillatorAccuracyTest) {
    static float _Complex v[2048];
    const int32_t offset = 12345;
    rtlsdr_frontend_t *lfe = create_frontend();
    double maxerr = 0;
    long n = 0;

    synthetic = 2L * INPUT_RATE;    /* one second */
    while (getSamples(lfe, v, 2048, offset) == 2048) {
        for (int k = 0; k < 2048; k++) {
            double w = -2.0 * M_PI * (double)offset * (double)(n + k + 1) / INPUT_RATE;
            double re = 72 / 128.0 * cos(w), im = 72 / 128.0 * sin(w);
            double e = hypot(crealf(v[k]) - re, cimagf(v[k]) - im);
            if (e > maxerr)
                maxerr = e;
        }
        n += 2048;
    }
    ck_assert_int_eq(n, INPUT_RATE);
    ck_assert_msg(maxerr < 1e-3, "oscillator error %g", maxerr);
    destroy_frontend(lfe);
} END_TEST

START_TEST(getSampleMatchesGetSamplesTest) {
    static float _Complex a[600], b[600];
    float abs;
    rtlsdr_frontend_t *lfe = create_frontend();

    synthetic = 2 * 600;
    ck_assert_int_eq(getSamples(lfe, a, 600, -777), 600);
    destroy_frontend(lfe);
    lfe = create_frontend();
    synthetic = 2 * 600;
    for (int k = 0; k < 600; k++) {
        ck_assert_int_eq(getSample(lfe, &b[k], &abs, -777), 1);
        ck_assert(fabsf(abs - jan_abs(b[k])) < 1e-6);
    }
    for (int k = 0; k < 600; k++)
        ck_assert(cabsf(a[k] - b[k]) < 1e-5);
    destroy_frontend(lfe);
} END_TEST

static Suite *input_sdr_suite(void) {
    Suite *s = suite_create("input_sdr");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, getSamplesTest);
    tcase_add_test(tc_core, oscillatorAccuracyTest);
    tcase_add_test(tc_core, getSampleMatchesGetSamplesTest);
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

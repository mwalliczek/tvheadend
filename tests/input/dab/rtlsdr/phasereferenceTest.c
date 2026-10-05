#include <check.h>
#include <fftw3.h>

#include "rtlsdr_private.h"
#include "phasereference.h"

extern float _Complex refTable[T_u];

#define SIGLEN (T_null + 2 * T_s)

static struct sdr_state_t sdr;
static float _Complex sig[SIGLEN], rx[SIGLEN];

/* null symbol followed by the phase reference symbol (with cyclic
 * prefix) and a data symbol with fixed QPSK carriers */
static void make_frame(void) {
    float _Complex *buf = fftwf_malloc(sizeof(float _Complex) * T_u);
    fftwf_plan plan = fftwf_plan_dft_1d(T_u, (float(*)[2])buf, (float(*)[2])buf,
                                        FFTW_BACKWARD, FFTW_ESTIMATE);
    int b, k;

    memset(sig, 0, sizeof(sig));
    for (b = 0; b < 2; b++) {
        float _Complex *out = &sig[T_null + b * T_s];
        for (k = 0; k < T_u; k++)
            buf[k] = b == 0 ? refTable[k] :
                     (refTable[k] != 0 ? cexpf(I * M_PI / 4 * (2 * (k % 4) + 1)) : 0);
        fftwf_execute(plan);
        for (k = 0; k < T_u; k++)
            out[T_g + k] = buf[k] / sqrtf(K);
        for (k = 0; k < T_g; k++)
            out[k] = out[T_u + k];
    }
    fftwf_destroy_plan(plan);
    fftwf_free(buf);
}

/* channel: direct path plus one echo, carrier offset in carriers */
static void channel(float echo_gain, int echo_delay, float offset) {
    int n;
    for (n = 0; n < SIGLEN; n++) {
        float _Complex s = sig[n];
        if (echo_delay && n >= echo_delay)
            s += echo_gain * sig[n - echo_delay];
        rx[n] = s * cexpf(I * 2 * M_PI * offset * n / T_u);
    }
}

/* the buffer handed to phaseReferenceFindIndex starts delta samples
 * after the begin of the cyclic prefix of the phase reference symbol,
 * the useful part of the first path then starts at T_g - delta */
START_TEST(findIndexDirectPathTest) {
    int delta;
    channel(0, 0, 0.1);
    for (delta = -10; delta <= 60; delta += 10)
        ck_assert_int_eq(phaseReferenceFindIndex(&sdr, &rx[T_null + delta]), T_g - delta);
} END_TEST

START_TEST(findIndexSfnTest) {
    /* a second transmitter 300 samples (146 us) later and stronger: the
     * window must start at the first path, the second one is covered by
     * the cyclic prefix */
    int delta;
    channel(1.5, 300, 0.1);
    for (delta = -10; delta <= 60; delta += 10)
        ck_assert_int_eq(phaseReferenceFindIndex(&sdr, &rx[T_null + delta]), T_g - delta);
} END_TEST

START_TEST(findIndexWeakFirstPathTest) {
    /* a first path below a third of the strongest one is ignored */
    channel(6.0, 150, 0.1);
    ck_assert_int_eq(phaseReferenceFindIndex(&sdr, &rx[T_null + 20]), T_g - 20 + 150);
} END_TEST

START_TEST(estimateOffsetTest) {
    int offset;
    for (offset = -30; offset <= 30; offset += 7) {
        channel(0, 0, offset + 0.2f);
        ck_assert_int_eq(phaseReferenceEstimateOffset(&sdr, &rx[T_null + T_g]), offset);
    }
} END_TEST

static Suite *phasereference_suite(void) {
    Suite *s = suite_create("phasereference");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, findIndexDirectPathTest);
    tcase_add_test(tc_core, findIndexSfnTest);
    tcase_add_test(tc_core, findIndexWeakFirstPathTest);
    tcase_add_test(tc_core, estimateOffsetTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr;

    initConstPhaseReference();
    initPhaseReference(&sdr);
    make_frame();

    sr = srunner_create(phasereference_suite());
    srunner_set_xml(sr, "phasereferenceTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    destroyPhaseReference(&sdr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

#include <check.h>
#include <fftw3.h>

#include "rtlsdr_private.h"
#include "ofdmDecoder.h"

/* the frequency interleaver: carrier k of the FFT carries soft bit i */
static int carrierOf(int i) {
    static int16_t map[K];
    static int init;
    if (!init) {
        int16_t tmp[T_u];
        int n = 0, j;
        tmp[0] = 0;
        for (j = 1; j < T_u; j++)
            tmp[j] = (13 * tmp[j - 1] + 511) % T_u;
        for (j = 0; j < T_u; j++)
            if (tmp[j] != T_u / 2 && tmp[j] >= 256 && tmp[j] <= 256 + K)
                map[n++] = tmp[j] - T_u / 2;
        init = 1;
    }
    return map[i] < 0 ? map[i] + T_u : map[i];
}

/* stubs from input_sdr.c / ficHandler.c / mscHandler.c */
void process_ficBlock(struct sdr_state_t *sdr, const int16_t data[], int16_t blkno);
void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno);
float jan_abs(float _Complex z) {
    return fabsf(crealf(z)) + fabsf(cimagf(z));
}

float get_db(float x) {
    return 20 * log10f((x + 1) / (float)(256));
}

static int16_t softbits[2 * K];

void process_ficBlock(struct sdr_state_t *sdr, const int16_t data[], int16_t blkno) {
    memcpy(softbits, data, sizeof(softbits));
}

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
}

/* one OFDM symbol (cyclic prefix + useful part) from its carriers */
static void symbol(const float _Complex *carriers, float _Complex *out) {
    float _Complex *buf = fftwf_malloc(sizeof(float _Complex) * T_u);
    fftwf_plan plan = fftwf_plan_dft_1d(T_u, (float(*)[2])buf, (float(*)[2])buf,
                                        FFTW_BACKWARD, FFTW_ESTIMATE);
    int k;
    memcpy(buf, carriers, sizeof(float _Complex) * T_u);
    fftwf_execute(plan);
    for (k = 0; k < T_u; k++)
        out[T_g + k] = buf[k] / T_u;
    for (k = 0; k < T_g; k++)
        out[k] = out[T_u + k];
    fftwf_destroy_plan(plan);
    fftwf_free(buf);
}

/* A carrier in a deep fade (multipath, SFN) carries mostly noise: its
 * soft bits must get a small weight instead of the full confidence of
 * a phase only decision, the sign must stay right. */
START_TEST(fadedCarrierTest) {
    struct sdr_state_t *sdr = calloc(1, sizeof(struct sdr_state_t));
    static float _Complex ref[T_u], data[T_u], sym[T_s];
    int i, faded = 100, deep = 200;

    sdr->mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    initConstOfdmDecoder();
    initOfdmDecoder(sdr);

    /* phase reference: all carriers 1, then QPSK data, bits alternating */
    for (i = 0; i < K; i++)
        ref[carrierOf(i)] = 1;
    for (i = 0; i < K; i++) {
        float re = (i & 1) ? -1 : 1, im = (i & 2) ? -1 : 1;
        float amp = i == faded ? 0.1f : i == deep ? 0.01f : 1.0f;
        data[carrierOf(i)] = amp * (re + I * im) / sqrtf(2);
        ref[carrierOf(i)] *= amp;       /* a static fade hits both symbols */
    }
    symbol(ref, sym);
    processBlock_0(sdr, &sym[T_g]);    /* starts after the cyclic prefix */
    symbol(data, sym);
    decodeBlock(sdr, sym, 1);

    for (i = 0; i < K; i++) {
        int bitRe = (i & 1) ? 1 : 0, bitIm = (i & 2) ? 1 : 0;
        /* a one is a positive soft bit */
        ck_assert_msg((softbits[i] > 0) == bitRe, "carrier %d re %d", i, softbits[i]);
        ck_assert_msg((softbits[K + i] > 0) == bitIm, "carrier %d im %d", i, softbits[K + i]);
        if (i != faded && i != deep) {
            ck_assert_int_ge(abs(softbits[i]), 80);
            ck_assert_int_le(abs(softbits[i]), 100);
        }
    }
    /* amplitude 0.1 -> power 0.01 of the average (differential product) */
    ck_assert_int_le(abs(softbits[faded]), 3);
    ck_assert_int_le(abs(softbits[deep]), 1);

    destroyOfdmDecoder(sdr);
    free(sdr->mmi);
    free(sdr);
} END_TEST

static Suite *ofdmDecoder_suite(void) {
    Suite *s = suite_create("ofdmDecoder");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, fadedCarrierTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(ofdmDecoder_suite());

    srunner_set_xml(sr, "ofdmDecoderTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

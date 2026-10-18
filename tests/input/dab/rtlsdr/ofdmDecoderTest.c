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
void process_mscBlock(struct sdr_state_t *sdr, const int16_t data[], int16_t blkno);
uint32_t msc_symbol_mask(struct sdr_state_t *sdr);
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

/* MSC symbols to demodulate, and per symbol of the frame what was handed on */
static uint32_t mscMask = 0x3FFFF;
static int16_t mscBits[L][2 * K];
static int mscDecoded[L], mscCalls[L];

uint32_t msc_symbol_mask(struct sdr_state_t *sdr) {
    return mscMask;
}

void process_mscBlock(struct sdr_state_t *sdr, const int16_t data[], int16_t blkno) {
    mscCalls[blkno]++;
    if (data == NULL)
        return;
    mscDecoded[blkno]++;
    memcpy(softbits, data, sizeof(softbits));
    memcpy(mscBits[blkno], data, sizeof(softbits));
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

/* The sample clock offset of the stick turns the phase of carrier k by
 * slope * k from symbol to symbol, a residual frequency error adds a
 * common phase. Here 0.3 samples per symbol (~120 ppm) and 20 degrees:
 * 60 degrees at the band edges, the QPSK decisions there would be wrong
 * without the correction. */
START_TEST(clockOffsetTest) {
    struct sdr_state_t *sdr = calloc(1, sizeof(struct sdr_state_t));
    static float _Complex car[T_u], sym[T_s];
    static int bits[2 * K];
    const float slope = 2 * M_PI * 0.3f / T_u, cpe = 20 * M_PI / 180;
    int i, s, errors = 0;

    sdr->mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    initConstOfdmDecoder();
    initOfdmDecoder(sdr);
    srand(7);

    for (i = 0; i < K; i++)
        car[carrierOf(i)] = 1;
    symbol(car, sym);
    processBlock_0(sdr, &sym[T_g]);
    for (s = 1; s < L; s++) {
        for (i = 0; i < K; i++) {
            int c = carrierOf(i), k = c > T_u / 2 ? c - T_u : c;
            bits[i] = rand() & 1;
            bits[K + i] = rand() & 1;
            car[c] *= ((bits[i] ? -1 : 1) + I * (bits[K + i] ? -1 : 1)) / sqrtf(2)
                      * cexpf(I * (slope * k + cpe));
        }
        symbol(car, sym);
        decodeBlock(sdr, sym, s);
        if (s < L / 2)
            continue;           /* the slope estimate settles */
        for (i = 0; i < K; i++) {
            /* a one is a positive soft bit */
            errors += (softbits[i] > 0) != bits[i];
            errors += (softbits[K + i] > 0) != bits[K + i];
        }
    }
    ck_assert_int_eq(errors, 0);
    ck_assert_msg(fabsf(sdr->ofdmDecoder.slope - slope) < 0.05f * slope,
                  "slope %g, expected %g", sdr->ofdmDecoder.slope, slope);

    destroyOfdmDecoder(sdr);
    free(sdr->mmi);
    free(sdr);
} END_TEST


/* Only the MSC symbols of the active sub-channels are demodulated. The
 * others are still handed on (as NULL, the CIF must be completed), and
 * the symbol before a demodulated one serves as its phase reference:
 * with a gap, across the CIF boundary (symbol 17 -> symbol 0 of the next
 * CIF) and after the FIC (the first symbol of the frame). */
START_TEST(mscSymbolMaskTest) {
    struct sdr_state_t *sdr = calloc(1, sizeof(struct sdr_state_t));
    static float _Complex car[T_u], sym[L][T_s];
    static int bits[L][2 * K];
    int i, s, errors = 0;

    sdr->mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    initConstOfdmDecoder();
    initOfdmDecoder(sdr);
    srand(11);

    /* one frame with random data */
    for (i = 0; i < K; i++)
        car[carrierOf(i)] = cexpf(I * 2 * M_PI * rand() / RAND_MAX);
    symbol(car, sym[0]);
    for (s = 1; s < L; s++) {
        for (i = 0; i < K; i++) {
            int c = carrierOf(i);
            bits[s][i] = rand() & 1;
            bits[s][K + i] = rand() & 1;
            car[c] *= ((bits[s][i] ? -1 : 1) + I * (bits[s][K + i] ? -1 : 1)) / sqrtf(2);
        }
        symbol(car, sym[s]);
    }

    /* symbol 0 (after the FIC), 9 (after a gap) and 17 (the last one) */
    mscMask = (1u << 0) | (1u << 9) | (1u << 17);
    memset(mscDecoded, 0, sizeof(mscDecoded));
    memset(mscCalls, 0, sizeof(mscCalls));
    processBlock_0(sdr, &sym[0][T_g]);
    for (s = 1; s < L; s++)
        decodeBlock(sdr, sym[s], s);

    for (s = 4; s < L; s++) {
        int cs = (s - 4) % numberofblocksperCIF;
        ck_assert_int_eq(mscCalls[s], 1);
        ck_assert_int_eq(mscDecoded[s], (mscMask >> cs) & 1);
        if (!mscDecoded[s])
            continue;
        for (i = 0; i < K; i++) {
            /* a one is a positive soft bit */
            errors += (mscBits[s][i] > 0) != bits[s][i];
            errors += (mscBits[s][K + i] > 0) != bits[s][K + i];
        }
    }
    ck_assert_int_eq(errors, 0);
    mscMask = 0x3FFFF;

    destroyOfdmDecoder(sdr);
    free(sdr->mmi);
    free(sdr);
} END_TEST

static Suite *ofdmDecoder_suite(void) {
    Suite *s = suite_create("ofdmDecoder");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, fadedCarrierTest);
    tcase_add_test(tc_core, clockOffsetTest);
    tcase_add_test(tc_core, mscSymbolMaskTest);
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

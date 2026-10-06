#include <check.h>

#include "protection.h"

/* ETSI EN 300 401 table 6: sub-channel size (CU), protection level, bit rate */
static const int uepTable[64][3] = {
    {16,5,32},{21,4,32},{24,3,32},{29,2,32},{35,1,32},
    {24,5,48},{29,4,48},{35,3,48},{42,2,48},{52,1,48},
    {29,5,56},{35,4,56},{42,3,56},{52,2,56},
    {32,5,64},{42,4,64},{48,3,64},{58,2,64},{70,1,64},
    {40,5,80},{52,4,80},{58,3,80},{70,2,80},{84,1,80},
    {48,5,96},{58,4,96},{70,3,96},{84,2,96},{104,1,96},
    {58,5,112},{70,4,112},{84,3,112},{104,2,112},
    {64,5,128},{84,4,128},{96,3,128},{116,2,128},{140,1,128},
    {80,5,160},{104,4,160},{116,3,160},{140,2,160},{168,1,160},
    {96,5,192},{116,4,192},{140,3,192},{168,2,192},{208,1,192},
    {116,5,224},{140,4,224},{168,3,224},{208,2,224},{232,1,224},
    {128,5,256},{168,4,256},{192,3,256},{232,2,256},{280,1,256},
    {160,5,320},{208,4,320},{280,2,320},
    {192,5,384},{280,3,384},{416,1,384}};

/* number of received (not punctured) bits the decoder expects */
static int usedBits(const protection_t *p) {
    int n = 0;
    for (int i = 0; i < p->indexTableSize; i++)
        n += p->indexTable[i];
    return n;
}

START_TEST(uepSizesTest) {
    initConstViterbi768();
    for (int i = 0; i < 64; i++) {
        protection_t *p = uep_protection_init(uepTable[i][2], uepTable[i][1]);
        int padding = uepTable[i][0] * 64 - usedBits(p);
        ck_assert_int_eq(p->outSize, 24 * uepTable[i][2]);
        ck_assert_msg(padding == 0 || padding == 4 || padding == 8,
            "UEP %d kbit/s level %d: %d bits do not fit into %d CU",
            uepTable[i][2], uepTable[i][1], usedBits(p), uepTable[i][0]);
        protection_destroy(p);
    }
} END_TEST

START_TEST(eepSizesTest) {
    /* sub-channel size in CU per n (A: n = bitrate / 8, B: n = bitrate / 32) */
    static const int sizeA[4] = { 12, 8, 6, 4 };
    static const int sizeB[4] = { 27, 21, 18, 15 };
    initConstViterbi768();
    for (int level = 0; level < 4; level++) {
        for (int n = 1; n <= 864 / sizeA[level]; n++) {
            protection_t *p = eep_protection_init(n * 8, level);
            ck_assert_int_eq(usedBits(p), sizeA[level] * n * 64);
            protection_destroy(p);
        }
        for (int n = 1; n <= 864 / sizeB[level]; n++) {
            protection_t *p = eep_protection_init(n * 32, level | 4);
            ck_assert_int_eq(usedBits(p), sizeB[level] * n * 64);
            protection_destroy(p);
        }
    }
} END_TEST

START_TEST(highBitrateDeconvolveTest) {
    /* 384 kbit/s: the index table has more than 32767 entries */
    protection_t *p;
    int16_t *in;
    uint8_t *out;
    initConstViterbi768();
    p = uep_protection_init(384, 1);
    ck_assert_int_gt(p->indexTableSize, 32767);
    in = calloc(416 * 64, sizeof(int16_t));
    out = calloc(p->outSize, sizeof(uint8_t));
    /* strong "0" soft bits decode to all zero data, i.e. the dispersal vector */
    for (int i = 0; i < 416 * 64; i++)
        in[i] = -127;
    protection_deconvolve(p, in, out);
    ck_assert_int_eq(memcmp(out, p->disperseVector, p->outSize), 0);
    free(in);
    free(out);
    protection_destroy(p);
} END_TEST

START_TEST(eepProtectionTest) {
    protection_t* protection;
    initConstViterbi768();
    protection = eep_protection_init(72, 2);
    ck_assert_int_eq(protection->outSize, 24 * 72);
    ck_assert_int_eq(usedBits(protection), 54 * 64);
    protection_destroy(protection);
} END_TEST

/* textbook DAB encoder (EN 300 401 11.1.1): generators 133, 171, 145, 133
 * (octal), the current bit is the MSB of the shift register */
static void encode(const uint8_t *u, int n, uint8_t *c) {
    static const int g[4] = { 0133, 0171, 0145, 0133 };
    int reg = 0;
    for (int i = 0; i < n + 6; i++) {
        reg = (reg >> 1) | ((i < n ? u[i] : 0) << 6);
        for (int k = 0; k < 4; k++)
            c[4 * i + k] = __builtin_parity(reg & g[k]);
    }
}

/* the channel bit errors are counted by re-encoding the decoded bits */
START_TEST(bitErrorCountTest) {
    protection_t *p;
    uint8_t *u, *c, *out;
    int16_t *in;
    int i, n = 0, flipped = 0;

    initConstViterbi768();
    p = eep_protection_init(64, 2);         /* EEP 3-A, 64 kbit/s */
    u = malloc(p->outSize);
    c = malloc(p->indexTableSize);
    out = malloc(p->outSize);
    in = calloc(p->indexTableSize, sizeof(int16_t));
    srand(5);
    for (i = 0; i < p->outSize; i++)
        u[i] = rand() & 1;
    encode(u, p->outSize, c);
    for (i = 0; i < p->indexTableSize; i++)
        if (p->indexTable[i])
            in[n++] = c[i] ? 100 : -100;

    protection_deconvolve(p, in, out);
    for (i = 0; i < p->outSize; i++)
        ck_assert_int_eq(out[i] ^ p->disperseVector[i], u[i]);
    ck_assert_int_eq(p->bits, n);
    ck_assert_int_eq(p->bitErrors, 0);

    /* sparse errors are corrected and counted */
    for (i = 7; i < n; i += 97, flipped++)
        in[i] = -in[i];
    protection_deconvolve(p, in, out);
    for (i = 0; i < p->outSize; i++)
        ck_assert_int_eq(out[i] ^ p->disperseVector[i], u[i]);
    ck_assert_int_eq(p->bits, 2 * n);
    ck_assert_int_eq(p->bitErrors, flipped);

    free(u);
    free(c);
    free(out);
    free(in);
    protection_destroy(p);
} END_TEST

static Suite *protection_suite(void) {
    Suite *s = suite_create("protection");
    TCase *tc_core = tcase_create("Core");
    tcase_set_timeout(tc_core, 30);
    tcase_add_test(tc_core, eepProtectionTest);
    tcase_add_test(tc_core, uepSizesTest);
    tcase_add_test(tc_core, eepSizesTest);
    tcase_add_test(tc_core, highBitrateDeconvolveTest);
    tcase_add_test(tc_core, bitErrorCountTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(protection_suite());

    srunner_set_xml(sr, "protectionTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

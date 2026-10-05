#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "reed-solomon.h"

/* reference encoder: GF(256) with x^8+x^4+x^3+x^2+1, generator roots
 * alpha^0 .. alpha^(nroots-1), as for DAB+ (RS(120,110)) and packet
 * mode FEC (RS(204,188)) */
static uint8_t gf_exp[512], gf_log[256];

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    return a && b ? gf_exp[gf_log[a] + gf_log[b]] : 0;
}

static void encode(const uint8_t *msg, int k, int nroots, uint8_t *cw) {
    uint8_t gen[17], buf[255];
    int i, j, x = 1;

    for (i = 0; i < 255; i++) {
        gf_exp[i] = gf_exp[i + 255] = x;
        gf_log[x] = i;
        x <<= 1;
        if (x & 0x100)
            x ^= 0x11D;
    }
    memset(gen, 0, sizeof(gen));
    gen[0] = 1;
    for (i = 0; i < nroots; i++)
        for (j = i + 1; j >= 0; j--)
            gen[j] = (j ? gen[j - 1] : 0) ^ gf_mul(gf_exp[i], gen[j]);

    memcpy(buf, msg, k);
    memset(buf + k, 0, nroots);
    for (i = 0; i < k; i++)
        if (buf[i])
            for (j = 1; j <= nroots; j++)
                buf[i + j] ^= gf_mul(buf[i], gen[nroots - j]);
    memcpy(cw, msg, k);
    memcpy(cw + k, buf + k, nroots);
}

static void check_code(int n, int nroots) {
    reedSolomon_t *rs = init_reedSolomon(8, 0435, 0, 1, nroots);
    int k = n - nroots, cut = 255 - n, errors, trial, i;
    uint8_t msg[255], cw[255], rx[255], out[255];

    srand(n);
    for (errors = 0; errors <= nroots / 2 + 1; errors++) {
        for (trial = 0; trial < 50; trial++) {
            for (i = 0; i < k; i++)
                msg[i] = rand();
            encode(msg, k, nroots, cw);
            memcpy(rx, cw, n);
            /* distinct positions, data and parity */
            for (i = 0; i < errors; i++)
                rx[(trial + i * 13) % n] ^= 1 + rand() % 255;
            int r = reedSolomon_dec(rs, rx, out, cut);
            if (errors <= nroots / 2) {
                ck_assert_msg(r >= 0, "RS(%d,%d) %d errors: %d", n, k, errors, r);
                ck_assert_msg(!memcmp(out, msg, k), "RS(%d,%d) %d errors: wrong data", n, k, errors);
            } else {
                /* too many: reported or at least never silently "corrected" to
                   the sent data without notice */
                ck_assert_msg(r < 0 || memcmp(out, msg, k), "RS(%d,%d) %d errors", n, k, errors);
            }
        }
    }
    destroy_reedSolomon(rs);
}

START_TEST(dabPlusTest) {
    check_code(120, 10);
} END_TEST

START_TEST(packetFecTest) {
    check_code(204, 16);
} END_TEST

START_TEST(checkedTest) {
    reedSolomon_t *rs = init_reedSolomon(8, 0435, 0, 1, 16);
    uint8_t msg[188], cw[204], out[188];
    int i, bad = 0, trial;

    /* random words are (almost) never accepted as correctable */
    srand(3);
    for (trial = 0; trial < 200; trial++) {
        for (i = 0; i < 204; i++)
            cw[i] = rand();
        if (reedSolomon_dec_checked(rs, cw, out, 51) >= 0)
            bad++;
    }
    ck_assert_int_le(bad, 1);

    for (i = 0; i < 188; i++)
        msg[i] = i;
    encode(msg, 188, 16, cw);
    cw[5] ^= 0x10;
    cw[100] ^= 0x22;
    ck_assert_int_eq(reedSolomon_dec_checked(rs, cw, out, 51), 2);
    ck_assert_int_eq(memcmp(out, msg, 188), 0);
    destroy_reedSolomon(rs);
} END_TEST

static Suite *reedSolomon_suite(void) {
    Suite *s = suite_create("reedSolomon");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, dabPlusTest);
    tcase_add_test(tc_core, packetFecTest);
    tcase_add_test(tc_core, checkedTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(reedSolomon_suite());

    srunner_set_xml(sr, "reed-solomonTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
